// LLM maintained. Reuse depth geometry; actual encoder queries and split attachment preservation.
#define main depth_main
#include "sulrim_depth.c"
#undef main

static sg_pipeline timing_compute;
static sg_buffer timing_result;
static sg_view timing_storage;
EXPORT void timing_init(void) {
    probe_init();
#if defined(SOKOL_METAL)
    const char* compute_source = "#include <metal_stdlib>\nusing namespace metal;\n"
        "kernel void cs(uint i [[thread_position_in_grid]], device uint* p [[buffer(8)]]) {p[i]+=1;}";
#else
    const char* compute_source = "@group(1) @binding(0) var<storage,read_write> p:array<u32>;"
        "@compute @workgroup_size(32) fn cs(@builtin(global_invocation_id) i:vec3u) {p[i.x]+=1;}";
#endif
    uint32_t zero[32] = {0};
    timing_result = sg_make_buffer(&(sg_buffer_desc){.data = SG_RANGE(zero), .usage = {.storage_buffer = true, .copy_src = true}});
    timing_storage = sg_make_view(&(sg_view_desc){.storage_buffer.buffer = timing_result});
    sg_shader cs = sg_make_shader(&(sg_shader_desc){.compute_func = {.source = compute_source, .entry = "cs"},
        .mtl_threads_per_threadgroup = {32,1,1}, .views[0].storage_buffer = {
            .stage = SG_SHADERSTAGE_COMPUTE, .msl_buffer_n = 8, .wgsl_group1_binding_n = 0}});
    timing_compute = sg_make_pipeline(&(sg_pipeline_desc){.compute = true, .shader = cs});
    CHECK(sg_query_pipeline_state(timing_compute) == SG_RESOURCESTATE_VALID);
    CHECK(sg_query_view_state(timing_storage) == SG_RESOURCESTATE_VALID);
}
static void compute_pass(sg_pass pass) {
    sg_begin_pass(&pass); sg_apply_pipeline(timing_compute);
    sg_apply_bindings(&(sg_bindings){.views[0] = timing_storage}); sg_dispatch(1,1,1); sg_end_pass();
}

#if defined(SOKOL_WGPU)
EXPORT WGPUQuerySet timing_queries(void) {
    WGPUQuerySetDescriptor desc = WGPU_QUERY_SET_DESCRIPTOR_INIT;
    desc.type = WGPUQueryType_Timestamp; desc.count = 4;
    WGPUQuerySet result = wgpuDeviceCreateQuerySet(_sg.wgpu.dev, &desc); CHECK(result); return result;
}
EXPORT void timing_drop_queries(WGPUQuerySet queries) { wgpuQuerySetDestroy(queries); wgpuQuerySetRelease(queries); }
EXPORT WGPUCommandEncoder timing_encoder(void) { return (WGPUCommandEncoder)sg_wgpu_command_encoder(); }
EXPORT WGPUBuffer timing_buffer(void) { return (WGPUBuffer)sg_wgpu_query_buffer_info(timing_result).buf; }
EXPORT void timing_arm(WGPUQuerySet queries) {
    CHECK(!sg_wgpu_frame_timestamp_passes());
    sg_wgpu_arm_frame_timestamps(queries);
}
EXPORT void timing_render(WGPUQuerySet queries, int armed, int per_pass, int copy_first) {
    CHECK(!color.id);
    if (copy_first) {
        uint32_t initial[4] = {1,2,3,4};
        sg_buffer source_buffer = sg_make_buffer(&(sg_buffer_desc){.data = SG_RANGE(initial), .usage.copy_src = true});
        sg_buffer target = sg_make_buffer(&(sg_buffer_desc){.size = sizeof initial, .usage.copy_dst = true});
        sg_copy_buffer_to_buffer(&(sg_copy_buffer_to_buffer_desc){.src.buffer = source_buffer, .dst.buffer = target, .size = sizeof initial});
        CHECK(sg_wgpu_command_encoder()); sg_destroy_buffer(source_buffer); sg_destroy_buffer(target);
    }
    timing_arm(armed ? queries : 0);
    color = sg_make_image(&(sg_image_desc){.width = 16, .height = 4, .pixel_format = SG_PIXELFORMAT_RGBA8,
        .usage = {.color_attachment = true, .copy_src = true}});
    sg_view cv = sg_make_view(&(sg_view_desc){.color_attachment.image = color});
    sg_image depth = sg_make_image(&(sg_image_desc){.width = 16, .height = 4, .pixel_format = SG_PIXELFORMAT_DEPTH,
        .usage.depth_stencil_attachment = true});
    sg_view dv = sg_make_view(&(sg_view_desc){.depth_stencil_attachment.image = depth});
    for (int part = 0; part < 2; part++) {
        sg_pass pass = {.attachments = {.colors[0] = cv, .depth_stencil = dv}, .wgpu.end_query = per_pass ? 2+part : 0,
            .action = {.colors[0] = {.load_action = part ? SG_LOADACTION_LOAD : SG_LOADACTION_CLEAR, .clear_value = {0,0,0,1}},
                .depth = {.load_action = part ? SG_LOADACTION_LOAD : SG_LOADACTION_CLEAR, .store_action = SG_STOREACTION_STORE, .clear_value = 1}}};
        sg_begin_pass(&pass); sg_apply_pipeline(pipelines[0]);
        if (!part) sg_apply_scissor_rect(0,0,8,4,true);
        sg_draw(part*3,3,1); sg_end_pass();
        CHECK(sg_wgpu_frame_timestamp_passes() == (armed ? (uint32_t)part+1 : 0));
    }
    compute_pass((sg_pass){.compute = true});
    CHECK(sg_wgpu_frame_timestamp_passes() == (armed ? 3u : 0u));
    sg_destroy_view(cv); sg_destroy_view(dv); sg_destroy_image(depth);
}
EXPORT void timing_commit(void) {
    sg_commit(); CHECK(!sg_wgpu_frame_timestamp_passes() && !_sg.wgpu.timestamp_query && !_sg.wgpu.pass_started);
}
#else
#include <objc/runtime.h>
static IMP original_present;
static unsigned presents;
static void observe_present(id commands, SEL selector, id drawable) {
    presents++; ((void (*)(id, SEL, id))original_present)(commands, selector, drawable);
}
static void metal_frame(int samples, bool timed, bool deferred, bool stencil, id<MTLCounterSampleBuffer> counters) {
    CAMetalLayer* layer = [CAMetalLayer layer]; layer.device = _sg.mtl.device;
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm; layer.drawableSize = CGSizeMake(16,4);
    layer.frame = CGRectMake(0,0,16,4); layer.framebufferOnly = NO;
    id<CAMetalDrawable> drawable = [layer nextDrawable]; CHECK(drawable);
    sg_pixel_format format = stencil ? SG_PIXELFORMAT_DEPTH_STENCIL : SG_PIXELFORMAT_DEPTH;
    sg_image depth = sg_make_image(&(sg_image_desc){.width = 16, .height = 4, .sample_count = samples,
        .pixel_format = format, .usage.depth_stencil_attachment = true});
    sg_image msaa = {0};
    if (samples > 1) msaa = sg_make_image(&(sg_image_desc){.width = 16, .height = 4, .sample_count = samples,
        .pixel_format = SG_PIXELFORMAT_BGRA8, .usage.color_attachment = true});
    sg_pipeline_desc desc = sg_query_pipeline_desc(pipelines[0]);
    desc.sample_count = samples; desc.colors[0].pixel_format = SG_PIXELFORMAT_BGRA8;
    desc.depth.pixel_format = format;
    if (stencil) desc.stencil = (sg_stencil_state){.enabled = true, .front = {.compare = SG_COMPAREFUNC_ALWAYS, .pass_op = SG_STENCILOP_REPLACE},
        .back = {.compare = SG_COMPAREFUNC_ALWAYS, .pass_op = SG_STENCILOP_REPLACE}, .read_mask = 255, .write_mask = 255, .ref = 7};
    sg_pipeline first = sg_make_pipeline(&desc);
    if (stencil) {desc.stencil.front.compare = desc.stencil.back.compare = SG_COMPAREFUNC_EQUAL; desc.stencil.write_mask = 0; desc.depth.compare = SG_COMPAREFUNC_ALWAYS;}
    sg_pipeline second = sg_make_pipeline(&desc);
    CHECK(sg_query_pipeline_state(first) == SG_RESOURCESTATE_VALID && sg_query_pipeline_state(second) == SG_RESOURCESTATE_VALID);
    sg_mtl_begin_frame();
    id<MTLCommandBuffer> commands = (__bridge id<MTLCommandBuffer>)sg_mtl_command_buffer();
    Method method = class_getInstanceMethod(object_getClass(commands), @selector(presentDrawable:)); CHECK(method);
    original_present = method_setImplementation(method, (IMP)observe_present); presents = 0;
    for (int part = 0; part < (deferred ? 2 : 1); part++) {
        sg_pass pass = {.swapchain = {.width = 16, .height = 4, .sample_count = samples,
            .color_format = SG_PIXELFORMAT_BGRA8, .depth_format = format, .metal = {
                .current_drawable = (__bridge const void*)drawable,
                .msaa_color_texture = msaa.id ? sg_mtl_query_image_info(msaa).tex[0] : 0,
                .depth_stencil_texture = sg_mtl_query_image_info(depth).tex[0]}},
            .metal = {.sample_buffer = timed ? (__bridge const void*)counters : 0, .sample_index = part*4, .defer_present = deferred},
            .action = {.colors[0] = {.load_action = part ? SG_LOADACTION_LOAD : SG_LOADACTION_CLEAR, .clear_value = {0,0,0,1}},
                .depth = {.load_action = part ? SG_LOADACTION_LOAD : SG_LOADACTION_CLEAR, .store_action = SG_STOREACTION_STORE, .clear_value = 1},
                .stencil = {.load_action = part ? SG_LOADACTION_LOAD : SG_LOADACTION_CLEAR, .store_action = SG_STOREACTION_STORE}}};
        sg_begin_pass(&pass); sg_apply_pipeline(part ? second : first);
        if (!part && deferred && !stencil) sg_apply_scissor_rect(0,0,8,4,true);
        sg_draw(part*3,3,1); sg_end_pass(); CHECK(presents == (deferred ? 0u : 1u));
        CHECK(!_sg.mtl.cur_drawable && (_sg.mtl.deferred_drawable == drawable) == deferred);
    }
    compute_pass((sg_pass){.compute = true, .metal = {.sample_buffer = timed ? (__bridge const void*)counters : 0, .sample_index = 8}});
    id<MTLBuffer> read = [_sg.mtl.device newBufferWithLength:1024 options:MTLResourceStorageModeShared]; CHECK(read);
    id<MTLBlitCommandEncoder> blit = [commands blitCommandEncoder]; CHECK(blit);
    [blit copyFromTexture:drawable.texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(16,4,1)
        toBuffer:read destinationOffset:0 destinationBytesPerRow:256 destinationBytesPerImage:1024];
    [blit endEncoding];
    sg_destroy_pipeline(first); sg_destroy_pipeline(second); sg_destroy_image(depth); if (msaa.id) sg_destroy_image(msaa);
    sg_commit(); CHECK(presents == 1 && !_sg.mtl.deferred_drawable); method_setImplementation(method, original_present);
    [commands waitUntilCompleted]; CHECK(commands.status == MTLCommandBufferStatusCompleted);
    const uint8_t* bytes = read.contents;
    for (int y = 0; y < 4; y++) for (int x = 0; x < 16; x++) {
        const uint8_t* p = bytes+y*256+x*4; bool green = deferred && (stencil || x >= 8);
        CHECK(p[0] == 0 && p[1] == (green ? 255 : 0) && p[2] == (green ? 0 : 255) && p[3] == 255);
    }
    if (timed) {
        NSData* data = [counters resolveCounterRange:NSMakeRange(0,10)]; CHECK(data.length == 10*sizeof(MTLCounterResultTimestamp));
        const MTLCounterResultTimestamp* ticks = data.bytes;
        for (unsigned i = 0; i < 10; i+=2) {
            CHECK(ticks[i].timestamp != MTLCounterErrorValue && ticks[i+1].timestamp != MTLCounterErrorValue);
            CHECK(ticks[i].timestamp && ticks[i+1].timestamp > ticks[i].timestamp);
        }
    }
    _SG_OBJC_RELEASE(read);
}
int main(void) {
    @autoreleasepool {
        timing_init();
        id<MTLCounterSet> set = nil;
        for (id<MTLCounterSet> candidate in _sg.mtl.device.counterSets)
            if ([candidate.name isEqualToString:MTLCommonCounterSetTimestamp]) set = candidate;
        CHECK(set && [_sg.mtl.device supportsCounterSampling:MTLCounterSamplingPointAtStageBoundary]);
        MTLCounterSampleBufferDescriptor* desc = [[MTLCounterSampleBufferDescriptor alloc] init];
        desc.counterSet = set; desc.sampleCount = 10; desc.storageMode = MTLStorageModeShared;
        NSError* error = nil; id<MTLCounterSampleBuffer> counters = [_sg.mtl.device newCounterSampleBufferWithDescriptor:desc error:&error];
        CHECK(counters && !error); _SG_OBJC_RELEASE(desc);
        unsigned frames = 0;
        for (int samples = 1; samples <= 4; samples*=4) {
            metal_frame(samples, false, false, false, counters); frames++;
            for (int stencil = 0; stencil < 2; stencil++) {
                metal_frame(samples, true, true, stencil, counters); frames++;
                metal_frame(samples, false, true, stencil, counters); frames++;
            }
        }
        sg_mtl_buffer_info info = sg_mtl_query_buffer_info(timing_result);
        const uint32_t* values = [(__bridge id<MTLBuffer>)info.buf[0] contents];
        for (int i = 0; i < 32; i++) CHECK(values[i] == frames);
        _SG_OBJC_RELEASE(counters); probe_shutdown();
        printf("Metal timing: %u frames, 640 exact pixels, compute words, render/compute samples, one present, MSAA/depth/stencil preserved PASS\n", frames);
    }
    return 0;
}
#endif
