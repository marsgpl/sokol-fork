// LLM maintained. Read actual Sokol/Wasm depth-rendered pixels in headless WebGPU.
const [directory, playwright] = Bun.argv.slice(2);
if (!directory || !playwright) throw Error('usage: bun sulrim-depth.mjs BUILD_DIRECTORY PLAYWRIGHT_MODULE');
const {chromium} = await import(playwright);
const host = Bun.serve({port: 0, fetch(request) {
    const path = new URL(request.url).pathname;
    return new Response(['/probe.mjs', '/probe.wasm'].includes(path) ? Bun.file(`${directory}${path}`) : '<!doctype html>', {
        headers: {'Cross-Origin-Opener-Policy': 'same-origin', 'Cross-Origin-Embedder-Policy': 'require-corp',
            'Content-Type': path.endsWith('.mjs') ? 'text/javascript' : path.endsWith('.wasm') ? 'application/wasm' : 'text/html'},
    });
}});
let browser;
try {
    browser = await chromium.launch({channel: 'chrome', headless: true, args: ['--enable-unsafe-webgpu', '--ignore-gpu-blocklist']});
    const page = await browser.newPage(), pageErrors = [];
    page.on('pageerror', error => pageErrors.push(error.message));
    await page.goto(`http://localhost:${host.port}`);
    const result = await page.evaluate(async () => {
        const adapter = await navigator.gpu?.requestAdapter();
        if (!adapter) throw Error('No WebGPU adapter');
        const device = await adapter.requestDevice(), errors = [], textures = [], fragments = [];
        device.addEventListener('uncapturederror', e => errors.push(e.error.message));
        const createTexture = device.createTexture.bind(device);
        device.createTexture = desc => { const tex = createTexture(desc); textures.push(tex); return tex; };
        const createPipeline = device.createRenderPipeline.bind(device);
        device.createRenderPipeline = desc => { fragments.push(desc.fragment?.targets.length ?? -1); return createPipeline(desc); };
        device.pushErrorScope('validation');
        const {default: create} = await import('/probe.mjs');
        const module = await create({preinitializedWebGPUDevice: device});
        module._probe_init();
        if (fragments.filter(n => n === -1).length !== 1 || fragments.filter(n => n === 0).length !== 1) throw Error(`Depth fragment states: ${fragments}`);
        let pixels = 0;
        for (const [width,height] of [[16,4],[32,8],[128,64],[64,32]]) for (let mode = 0; mode < 5; mode++) {
            module._probe_render(mode, width, height);
            const depth = textures.findLast(t => t.label === 'sulrim-depth');
            const usage = mode === 4 ? GPUTextureUsage.RENDER_ATTACHMENT : GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING;
            if (!depth || depth.format !== (mode === 1 ? 'depth16unorm' : 'depth32float') || depth.width !== width || depth.height !== height || depth.usage !== usage) throw Error('Wrong native depth descriptor');
            const texture = module.WebGPU.getJsObject(module._probe_texture());
            const pitch = Math.ceil(width*4/256)*256;
            const read = device.createBuffer({size: pitch*height, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ});
            const enc = device.createCommandEncoder();
            enc.copyTextureToBuffer({texture}, {buffer: read, bytesPerRow: pitch}, [width,height]);
            device.queue.submit([enc.finish()]); await read.mapAsync(GPUMapMode.READ);
            const bytes = new Uint8Array(read.getMappedRange());
            for (let y = 0; y < height; y++) for (let x = 0; x < width; x++) {
                const at = y*pitch+x*4, green = mode === 2 || mode === 3 ? mode === 3 && (x & 1) === 0 : (Math.floor(x/(width/4)) & 1) !== 0;
                const expected = green ? [0,255,0,255] : mode < 2 || mode === 4 ? [255,0,0,255] : [0,0,0,255];
                if (expected.some((v,i) => bytes[at+i] !== v)) throw Error(`depth mode ${mode} ${width}x${height} pixel ${x}/${y}: ${bytes.slice(at,at+4)}`);
                pixels++;
            }
            read.unmap(); read.destroy(); module._probe_release();
        }
        module._probe_shutdown();
        const error = await device.popErrorScope();
        if (error) errors.push(error.message);
        if (errors.length) throw Error(errors.join('\n'));
        device.destroy();
        return {browser: navigator.userAgent, adapter: {vendor: adapter.info.vendor, architecture: adapter.info.architecture}, pixels};
    });
    if (pageErrors.length) throw Error(pageErrors.join('\n'));
    console.log(JSON.stringify(result, null, 2));
} finally { await browser?.close(); host.stop(true); }
