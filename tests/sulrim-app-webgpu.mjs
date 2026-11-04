// LLM maintained. Real app/device setup with optional features off and on.
const {chromium} = await import(Bun.argv[3]);
const directory = Bun.argv[2];
const host = Bun.serve({port: 0, fetch(request) {
    const path = new URL(request.url).pathname;
    return new Response(['/probe.mjs', '/probe.wasm'].includes(path)
        ? Bun.file(`${directory}${path}`)
        : '<!doctype html><canvas id="canvas" style="width:32px;height:32px"></canvas>',
        {headers: {'Content-Type': path.endsWith('.mjs') ? 'text/javascript'
            : path.endsWith('.wasm') ? 'application/wasm' : 'text/html'}});
}});
let browser;
try {
    browser = await chromium.launch({channel: 'chrome', headless: true,
        args: ['--enable-unsafe-webgpu', '--ignore-gpu-blocklist']});
    for (const enabled of [false, true]) {
        const page = await browser.newPage({deviceScaleFactor: 3});
        const failures = [];
        page.on('pageerror', error => failures.push(error.message));
        await page.goto(`http://localhost:${host.port}`);
        const result = await page.evaluate(async enabled => {
            const requestAdapter = navigator.gpu.requestAdapter.bind(navigator.gpu);
            let options, adapter;
            navigator.gpu.requestAdapter = async opts => {
                options = opts;
                return adapter = await requestAdapter(opts);
            };
            const {default: create} = await import('/probe.mjs');
            const module = await create({canvas: document.querySelector('#canvas')});
            module._probe_start(Number(enabled));
            for (let i = 0; !module._probe_ready(); ++i) {
                if (i === 1000) throw Error('App initialization timed out');
                await new Promise(resolve => setTimeout(resolve, 10));
            }
            if ((options?.powerPreference === 'low-power') !== enabled) {
                throw Error('Adapter preference differs from descriptor');
            }
            module._probe_features();
            const device = module.WebGPU.getJsObject(module._probe_device());
            const errors = [];
            device.addEventListener('uncapturederror', event => errors.push(event.error.message));
            const canvas = document.querySelector('#canvas');
            const initial = module.WebGPU.getJsObject(module._probe_texture(0));
            for (let i = 0; i < 10; ++i) module._probe_resize();
            if (module.WebGPU.getJsObject(module._probe_texture(0)) !== initial) throw Error('No-op resize recreated textures');
            for (const [width, height] of [[40, 24], [32, 32]]) {
                canvas.style.width = `${width}px`;
                canvas.style.height = `${height}px`;
                module._probe_resize();
                for (const depth of [0, 1]) {
                    const texture = module.WebGPU.getJsObject(module._probe_texture(depth));
                    if (canvas.width !== width * 3 || canvas.height !== height * 3 ||
                        texture.width !== canvas.width || texture.height !== canvas.height) {
                        throw Error('Resize lost native DPR or framebuffer dimensions');
                    }
                    if (depth && texture.format !== 'depth32float') throw Error('Unexpected stencil plane');
                }
            }
            if (module._probe_capabilities_freed() !== 1) throw Error('Surface capabilities not freed');
            for (let cycle = 0; cycle < 3; ++cycle) {
                const retired = [0, 1].map(depth => module.WebGPU.getJsObject(module._probe_texture(depth)));
                module._probe_recreate_surface();
                if (module._probe_capabilities_freed() !== cycle + 2) throw Error('Surface capabilities leaked');
                for (const [depth, texture] of retired.entries()) {
                    device.pushErrorScope('validation');
                    const encoder = device.createCommandEncoder();
                    encoder.beginRenderPass(depth ? {
                        colorAttachments: [], depthStencilAttachment: {view: texture.createView(),
                            depthLoadOp: 'clear', depthStoreOp: 'discard', depthClearValue: 1},
                    } : {colorAttachments: [{view: texture.createView(), loadOp: 'clear',
                        storeOp: 'discard', clearValue: {r: 0, g: 0, b: 0, a: 0}}]}).end();
                    device.queue.submit([encoder.finish()]);
                    const error = await device.popErrorScope();
                    if (!error || !/destroy/i.test(error.message)) throw Error('Retired surface texture was not destroyed');
                }
            }
            await device.queue.onSubmittedWorkDone();
            if (errors.length) throw Error(errors.join('\n'));
            const {vendor, architecture, device: gpu, description} = adapter.info;
            return {enabled, browser: navigator.userAgent, adapter: {vendor, architecture, device: gpu, description},
                features: [...device.features], native_dpr: 3, resizes: 2, no_op_resizes: 10,
                destroyed_surface_textures: 6, capabilities_freed: module._probe_capabilities_freed()};
        }, enabled);
        if (failures.length) throw Error(failures.join('\n'));
        console.log(JSON.stringify(result));
        await page.close();
    }
} finally {
    await browser?.close();
    host.stop(true);
}
