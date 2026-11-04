// LLM maintained. Actual Sokol uniform bytes across frames in headless Chrome.
const [directory, playwright] = Bun.argv.slice(2);
if (!directory || !playwright) throw Error('usage: bun sulrim-uniforms.mjs BUILD_DIRECTORY PLAYWRIGHT_MODULE');
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
    page.on('pageerror', error => pageErrors.push(error.message)); await page.goto(`http://localhost:${host.port}`);
    const result = await page.evaluate(async () => {
        const adapter = await navigator.gpu?.requestAdapter(); if (!adapter) throw Error('No WebGPU adapter');
        const device = await adapter.requestDevice(), errors = [];
        device.addEventListener('uncapturederror', e => errors.push(e.error.message)); device.pushErrorScope('validation');
        const counts = Array(13).fill(0);
        function observe(object, name, index, bytes) {
            const operation = object[name];
            object[name] = function(...args) {
                counts[index]++;
                if (bytes !== undefined) counts[index+1] += bytes(args);
                return operation.apply(this,args);
            };
        }
        observe(GPUCommandEncoder.prototype,'beginRenderPass',0);
        observe(GPUCommandEncoder.prototype,'beginComputePass',1);
        observe(device.queue,'writeBuffer',5,args => {
            const stride = args[2].BYTES_PER_ELEMENT ?? 1;
            return (args[4] ?? args[2].byteLength/stride-(args[3] ?? 0))*stride;
        });
        observe(device.queue,'submit',7);
        observe(device,'createBindGroup',8);
        observe(device.queue,'writeTexture',9,args => args[1].byteLength);
        observe(GPUCommandEncoder.prototype,'copyBufferToBuffer',11,args => args[4]);
        const uniformLayouts = new WeakMap(), makeLayout = device.createBindGroupLayout.bind(device), makeGroup = device.createBindGroup.bind(device);
        let uniformSizes = 0;
        device.createBindGroupLayout = desc => {
            const uniforms = desc.entries.filter(e => e.buffer?.type === 'uniform');
            if (uniforms.some(e => !e.buffer.minBindingSize)) throw Error('Missing uniform minimum size');
            const layout = makeLayout(desc); uniformLayouts.set(layout,uniforms); uniformSizes += uniforms.length; return layout;
        };
        device.createBindGroup = desc => {
            for (const u of uniformLayouts.get(desc.layout) ?? []) {
                if (Number(desc.entries.find(e => e.binding === u.binding)?.resource?.size) !== Number(u.buffer.minBindingSize)) throw Error('Wrong uniform binding size');
            }
            return makeGroup(desc);
        };
        let bindCalls = 0, checkedDraws = 0;
        for (const prototype of [GPURenderPassEncoder.prototype, GPUComputePassEncoder.prototype]) {
            observe(prototype,'setPipeline',2);
            const bindings = new WeakMap(), set = prototype.setBindGroup;
            prototype.setBindGroup = function(index, group, ...rest) {
                if (!group) throw Error('Null group reaches WebGPU');
                counts[index === 0 ? 3 : 4]++;
                const slots = bindings.get(this) ?? new Set(); slots.add(index); bindings.set(this,slots); bindCalls++;
                return set.call(this,index,group,...rest);
            };
            for (const name of ['draw','dispatchWorkgroups']) if (prototype[name]) {
                const operation = prototype[name];
                prototype[name] = function(...args) {
                    const slots = bindings.get(this);
                    if (!slots?.has(0) || !slots?.has(1)) throw Error('Unset pipeline layout group');
                    checkedDraws++; return operation.apply(this,args);
                };
            }
        }
        const {default: create} = await import('/probe.mjs');
        const module = await create({preinitializedWebGPUDevice: device}); module._probe_init();
        function checkCounts(enabled = true) {
            for (let i = 0; i < counts.length; i++) {
                const reported = module._probe_native_count(i);
                if (reported !== (enabled ? counts[i] : 0)) throw Error(`native counter ${i}: ${reported} vs ${counts[i]}, enabled=${enabled}`);
            }
            counts.fill(0);
        }
        let pixels = 0;
        for (let frame = 0; frame < 7; frame++) {
            module._probe_profile(frame < 5 ? [0,1,2,3,1][frame] : 0);
            if (frame < 5) module._probe_render(); else module._probe_resources(frame-5);
            checkCounts(frame !== 3);
            const texture = module.WebGPU.getJsObject(module._probe_texture());
            const read = device.createBuffer({size: 256, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ});
            const enc = device.createCommandEncoder(); enc.copyTextureToBuffer({texture}, {buffer: read, bytesPerRow: 256}, [8,1]);
            device.queue.submit([enc.finish()]); await read.mapAsync(GPUMapMode.READ);
            const bytes = new Uint8Array(read.getMappedRange());
            for (let x = 0; x < 8; x++) {
                const expected = frame < 5 ? (x === 3 ? [0,255,0,255] : [255,0,0,255]) : x === 2 ? [0,0,255,255] : x === 4 || x === 6 ? [0,255,0,255] : [255,0,0,255];
                if (expected.some((v,i) => bytes[x*4+i] !== v)) throw Error(`uniform frame ${frame} pixel ${x}: ${bytes.slice(x*4,x*4+4)}`);
                pixels++;
            }
            read.unmap(); read.destroy();
            counts.fill(0); // Caller-owned readback submits stay outside backend totals.
        }
        module._probe_transfers(); checkCounts();
        const source = module.WebGPU.getJsObject(module._probe_transfer_buffer());
        const texture = module.WebGPU.getJsObject(module._probe_transfer_image());
        const read = device.createBuffer({size: 776, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ});
        const enc = device.createCommandEncoder(); enc.copyBufferToBuffer(source,0,read,768,8);
        for (let mipLevel = 0; mipLevel < 2; mipLevel++) enc.copyTextureToBuffer({texture,mipLevel}, {buffer:read,offset:mipLevel*512,bytesPerRow:256}, mipLevel ? [1,1] : [2,2]);
        device.queue.submit([enc.finish()]); await read.mapAsync(GPUMapMode.READ);
        const bytes = new Uint8Array(read.getMappedRange());
        const expected = [1,2,3,4,5,6,7,0,9,10,11,12,13,14,15,16,17,18,19,20];
        const actual = [...bytes.slice(0,8),...bytes.slice(256,264),...bytes.slice(512,516)];
        if (expected.some((v,i) => actual[i] !== v) || expected.slice(0,8).some((v,i) => bytes[768+i] !== v)) throw Error('Transfer bytes differ');
        read.unmap(); read.destroy(); module._probe_transfer_cleanup();
        const error = await device.popErrorScope(); if (error) errors.push(error.message);
        if (errors.length) throw Error(errors.join('\n'));
        device.pushErrorScope('validation'); module._probe_bad_layout();
        const badLayout = await device.popErrorScope();
        if (!badLayout || !badLayout.message.includes('2400') || !badLayout.message.includes('16')) throw Error(`Expected early uniform-size rejection: ${badLayout?.message}`);
        device.pushErrorScope('validation'); module._probe_shutdown();
        const shutdownError = await device.popErrorScope(); if (shutdownError) throw shutdownError;
        if (errors.length) throw Error(errors.join('\n')); device.destroy();
        return {browser: navigator.userAgent, adapter: {vendor: adapter.info.vendor, architecture: adapter.info.architecture}, pixels, bindCalls, checkedDraws, uniformSizes, nativeCounters: 13, transferBytes: 28, profilingModes: ['default off','on','off','stats off','on'], earlySizeRejection: badLayout.message};
    });
    if (pageErrors.length) throw Error(pageErrors.join('\n')); console.log(JSON.stringify(result, null, 2));
} finally { await browser?.close(); host.stop(true); }
