// LLM maintained. Real pass descriptors, query readback, ownership and pixels in headless Chrome.
const [directory, playwright] = Bun.argv.slice(2);
if (!directory || !playwright) throw Error('usage: bun sulrim-timing.mjs BUILD_DIRECTORY PLAYWRIGHT_MODULE');
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
    const page = await browser.newPage(), errors = [];
    page.on('pageerror', error => errors.push(error.message));
    page.on('console', message => {if (message.type() === 'error') errors.push(message.text());});
    await page.goto(`http://localhost:${host.port}`);
    const result = await page.evaluate(async () => {
        const {default: create} = await import('/probe.mjs');
        const results = [];
        for (const enabled of [false, true]) {
            const adapter = await navigator.gpu?.requestAdapter();
            if (!adapter?.features.has('timestamp-query')) throw Error('Timestamp-query device required for this fixture');
            const device = await adapter.requestDevice({requiredFeatures: enabled ? ['timestamp-query'] : []});
            const deviceErrors = [], passes = [], queryObjects = [];
            device.addEventListener('uncapturederror', e => deviceErrors.push(e.error.message));
            device.pushErrorScope('validation');
            const createEncoder = device.createCommandEncoder.bind(device);
            device.createCommandEncoder = desc => {
                const encoder = createEncoder(desc);
                for (const name of ['beginRenderPass', 'beginComputePass']) {
                    const begin = encoder[name].bind(encoder);
                    encoder[name] = pass => {passes.push({kind: name, writes: pass.timestampWrites && {...pass.timestampWrites}}); return begin(pass);};
                }
                return encoder;
            };
            const createQueries = device.createQuerySet.bind(device);
            let destroyed = 0;
            device.createQuerySet = desc => {
                const queries = createQueries(desc), destroy = queries.destroy.bind(queries);
                queries.destroy = () => {destroyed++; destroy();}; queryObjects.push(queries); return queries;
            };
            const module = await create({preinitializedWebGPUDevice: device});
            module._timing_init();
            const handle = enabled ? module._timing_queries() : 0;
            const queries = handle ? module.WebGPU.getJsObject(handle) : null;
            // Empty commits disarm without creating an encoder or touching the borrowed query set.
            module._timing_arm(handle); module._timing_commit();
            const ticks = [];
            let checked = 0;
            for (let frame = 0; frame < 6; frame++) {
                const armed = enabled && frame !== 2, perPass = (frame & 1) === 0;
                passes.length = 0;
                module._timing_render(handle, +armed, +perPass, +(frame === 0));
                if (passes.length !== 3) throw Error(`Pass count ${passes.length}`);
                for (let i = 0; i < 3; i++) {
                    const pass = passes[i], writes = pass.writes;
                    if (pass.kind !== (i === 2 ? 'beginComputePass' : 'beginRenderPass')) throw Error('Pass kind mismatch');
                    if (!armed) {if (writes) throw Error('Unarmed pass writes timestamps'); continue;}
                    const end = perPass && i < 2 ? 2+i : 1;
                    if (writes?.querySet !== queries || writes.endOfPassWriteIndex !== end ||
                        writes.beginningOfPassWriteIndex !== (i === 0 ? 0 : undefined)) throw Error(`Wrong timestamp writes ${JSON.stringify(writes)}`);
                }
                const encoder = module.WebGPU.getJsObject(module._timing_encoder());
                const read = device.createBuffer({size: 1280, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ});
                const resolve = armed ? device.createBuffer({size: 32, usage: GPUBufferUsage.QUERY_RESOLVE | GPUBufferUsage.COPY_SRC}) : null;
                if (armed) {encoder.resolveQuerySet(queries, 0, 4, resolve, 0); encoder.copyBufferToBuffer(resolve, 0, read, 1152, 32);}
                const texture = module.WebGPU.getJsObject(module._probe_texture());
                encoder.copyTextureToBuffer({texture}, {buffer: read, bytesPerRow: 256}, [16,4]);
                encoder.copyBufferToBuffer(module.WebGPU.getJsObject(module._timing_buffer()), 0, read, 1024, 128);
                module._timing_commit(); module._probe_release();
                if (frame === 5) module._probe_shutdown();
                const validation = await device.popErrorScope(); if (validation) throw Error(validation.message);
                device.pushErrorScope('validation');
                if (destroyed) throw Error('Borrowed query set destroyed before GPU completion');
                await read.mapAsync(GPUMapMode.READ);
                const bytes = new Uint8Array(read.getMappedRange());
                for (let y = 0; y < 4; y++) for (let x = 0; x < 16; x++) {
                    const at = y*256+x*4, expected = x < 8 ? [255,0,0,255] : [0,255,0,255];
                    if (expected.some((v,i) => bytes[at+i] !== v)) throw Error(`Pixel ${frame}/${x}/${y}: ${bytes.slice(at,at+4)}`);
                    checked++;
                }
                if (new Uint32Array(bytes.buffer, 1024, 32).some(v => v !== frame+1)) throw Error('Wrong compute words');
                if (armed) {
                    const sample = [...new BigUint64Array(bytes.buffer, 1152, 4)];
                    if (sample[0] === 0n || sample[1] === 0n || (perPass && (sample[2] <= sample[0] || sample[3] === 0n))) throw Error(`Missing real pass samples ${sample}`);
                    ticks.push(sample.map(String));
                }
                read.unmap(); read.destroy(); resolve?.destroy();
            }
            if (handle) module._timing_drop_queries(handle);
            if (destroyed !== +enabled || queryObjects.length !== +enabled) throw Error('Wrong query ownership');
            const error = await device.popErrorScope(); if (error) deviceErrors.push(error.message);
            if (deviceErrors.length) throw Error(deviceErrors.join('\n'));
            results.push({enabled, frames: 6, checked, samples: ticks}); device.destroy();
        }
        return {browser: navigator.userAgent, results};
    });
    if (errors.length) throw Error(errors.join('\n'));
    console.log(JSON.stringify(result, null, 2));
} finally {await browser?.close(); host.stop(true);}
