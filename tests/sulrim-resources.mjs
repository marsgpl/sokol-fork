// LLM maintained. Actual Wasm/backend bytes; mock call counts are a separate gate.
const [directory, playwright] = Bun.argv.slice(2);
if (!directory || !playwright) throw Error('usage: bun sulrim-resources.mjs BUILD_DIRECTORY PLAYWRIGHT_MODULE');
const {chromium} = await import(playwright);
const host = Bun.serve({port: 0, fetch(request) {
    const path = new URL(request.url).pathname;
    return new Response(['/probe.mjs', '/probe.wasm'].includes(path) ? Bun.file(`${directory}${path}`) : '<!doctype html>', {
        headers: {
            'Cross-Origin-Opener-Policy': 'same-origin', 'Cross-Origin-Embedder-Policy': 'require-corp',
            'Content-Type': path.endsWith('.mjs') ? 'text/javascript' : path.endsWith('.wasm') ? 'application/wasm' : 'text/html',
        },
    });
}});
let browser;
try {
    browser = await chromium.launch({channel: 'chrome', headless: true, args: ['--enable-unsafe-webgpu', '--ignore-gpu-blocklist']});
    const page = await browser.newPage();
    const pageErrors = [];
    page.on('pageerror', error => pageErrors.push(error.message));
    await page.goto(`http://localhost:${host.port}`);
    const result = await page.evaluate(async () => {
        const adapter = await navigator.gpu?.requestAdapter();
        if (!adapter) throw Error('No WebGPU adapter');
        const bc = adapter.features.has('texture-compression-bc');
        const device = await adapter.requestDevice({requiredFeatures: bc ? ['texture-compression-bc'] : []});
        const errors = [], events = [];
        device.addEventListener('uncapturederror', e => errors.push(e.error.message));
        for (const [prototype, method, event] of [
            [GPUQueue.prototype, 'submit', 'submit'], [GPUBuffer.prototype, 'destroy', 'buffer'],
            [GPUTexture.prototype, 'destroy', 'image'], [GPUCommandEncoder.prototype, 'copyBufferToBuffer', 'use-buffer'],
            [GPUCommandEncoder.prototype, 'copyTextureToTexture', 'use-image'],
        ]) {
            const original = prototype[method];
            prototype[method] = function (...args) {
                const label = event === 'submit' ? '' : event.startsWith('use-') ? (args[0].texture ?? args[0]).label : this.label;
                if (event === 'submit' || label === `sulrim-retired-${event.endsWith('buffer') ? 'buffer' : 'image'}`) events.push(event);
                return original.apply(this, args);
            };
        }
        device.pushErrorScope('validation');
        const {default: create} = await import('/probe.mjs');
        const module = await create({preinitializedWebGPUDevice: device});
        module._probe_init(); module._probe_prepare();
        let checks = 0;
        const equal = (bytes, expected, message) => {
            for (let i = 0; i < expected.length; i++) if (bytes[i] !== expected[i]) throw Error(`${message} byte ${i}: ${bytes[i]} != ${expected[i]}`);
            checks++;
        };
        const buffer = module.WebGPU.getJsObject(module._probe_buffer());
        const read = device.createBuffer({size: 64, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ});
        let encoder = device.createCommandEncoder();
        encoder.copyBufferToBuffer(buffer, 0, read, 0, 64); device.queue.submit([encoder.finish()]);
        await read.mapAsync(GPUMapMode.READ);
        equal(new Uint8Array(read.getMappedRange()), module.HEAPU8.slice(module._probe_expected_buffer(), module._probe_expected_buffer() + 64), 'persistent/grown');
        read.unmap(); read.destroy();
        const textures = [];
        for (let index = 0; index < 7; index++) {
            const handle = module._probe_image(index);
            if (!handle) { if (index < 3 || index >= 5 || bc) throw Error(`missing image ${index}`); continue; }
            const texture = module.WebGPU.getJsObject(handle); textures.push(texture);
            for (let mip = 0; mip < (index >= 5 ? 1 : index >= 3 ? 4 : index === 2 ? 1 : 3); mip++) {
                const side = index >= 5 ? 2 : (index >= 3 ? 8 : 4) >> mip;
                const slices = index >= 5 ? 2 : 1, compressed = index === 3 || index === 4;
                const row = compressed ? Math.ceil(side / 4) * 8 : side * 4;
                const rows = compressed ? Math.ceil(side / 4) : side;
                const staging = device.createBuffer({size: rows * 256 * slices, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ});
                encoder = device.createCommandEncoder();
                const extent = compressed ? Math.ceil(side / 4) * 4 : side;
                encoder.copyTextureToBuffer({texture, mipLevel: mip}, {buffer: staging, bytesPerRow: 256, rowsPerImage: rows}, [extent, extent, slices]);
                device.queue.submit([encoder.finish()]); await staging.mapAsync(GPUMapMode.READ);
                const bytes = new Uint8Array(staging.getMappedRange());
                for (let slice = 0; slice < slices; slice++) {
                    const pointer = module._probe_expected_image(index, mip, slice), base = slice * rows * 256;
                    for (let y = 0; y < rows; y++) equal(bytes.subarray(base + y * 256, base + y * 256 + row), module.HEAPU8.slice(pointer + y * row, pointer + (y + 1) * row), `image ${index} mip ${mip} slice ${slice} row ${y}`);
                }
                staging.unmap(); staging.destroy();
            }
        }
        for (const role of ['buffer', 'image']) {
            const use = events.lastIndexOf(`use-${role}`), destroy = events.indexOf(role);
            const submit = events.indexOf('submit', use + 1);
            if (use < 0 || !(use < submit && submit < destroy)) throw Error(`retirement order ${role}: ${events}`);
        }
        module._probe_shutdown();
        const validation = await device.popErrorScope();
        if (validation) errors.push(validation.message);
        if (errors.length) throw Error(errors.join('\n'));
        // Retaining JS wrappers must not retain the owned GPU allocation after shutdown.
        for (const resource of [buffer, ...textures]) {
            device.pushErrorScope('validation');
            encoder = device.createCommandEncoder();
            let staging;
            if (resource === buffer) encoder.clearBuffer(resource);
            else {
                staging = device.createBuffer({size: 2048, usage: GPUBufferUsage.COPY_DST});
                encoder.copyTextureToBuffer({texture: resource}, {buffer: staging, bytesPerRow: 256}, [resource.width, resource.height, 1]);
            }
            device.queue.submit([encoder.finish()]);
            const error = await device.popErrorScope();
            staging?.destroy();
            if (!error || !/destroyed/i.test(error.message)) throw Error(`owned GPU resource survived shutdown: ${error?.message}`);
        }
        await device.queue.onSubmittedWorkDone(); device.destroy();
        const {vendor, architecture, device: gpu, description} = adapter.info;
        return {browser: navigator.userAgent, adapter: {vendor, architecture, device: gpu, description}, bc1: bc, checks, retiredResources: ['buffer', 'image'], shutdownChecks: textures.length + 1};
    });
    if (pageErrors.length) throw Error(pageErrors.join('\n'));
    console.log(JSON.stringify(result, null, 2));
} finally {
    await browser?.close(); host.stop(true);
}
