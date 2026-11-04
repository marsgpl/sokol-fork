// LLM maintained. Shared real-GPU uniform identity workload; no window or game.
#define SOKOL_IMPL
#define SOKOL_TRACE_HOOKS
#define SOKOL_VALIDATE_NON_FATAL
#include "sokol_gfx.h"
#include <stdio.h>
#include <stdlib.h>
#if defined(SOKOL_WGPU)
#include <emscripten.h>
extern WGPUDevice emscripten_webgpu_get_device(void);
#define EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define EXPORT
#endif
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); abort(); } } while (0)
static sg_shader shaders[3];
static sg_pipeline pipelines[4];
static sg_image image;
static sg_view color;
static uint64_t red_id, green_id;
static unsigned traces;
#if defined(SOKOL_METAL)
static id<MTLCommandBuffer> frame_command;
#endif
static float red[600], green[600];
EXPORT void probe_profile(int mode) {
    if (mode == 3) sg_disable_stats(); else sg_enable_stats();
    sg_wgpu_profile_uniforms(mode == 1);
}
static void logger(const char* tag, uint32_t level, uint32_t item, const char* message, uint32_t line, const char* file, void* data) {
    (void)tag; (void)item; (void)file; (void)data;
    if (level <= 1) { fprintf(stderr, "%u: %s\n", line, message ? message : "error"); abort(); }
}
static void trace_uniform(int slot, const sg_range* data, void* user) { (void)slot; (void)data; (void)user; traces++; }
#if defined(SOKOL_METAL)
static const char* fragment_source = "#include <metal_stdlib>\nusing namespace metal;\n"
    "struct U { float4 values[150]; };"
    "vertex float4 vs(uint v [[vertex_id]]) { float2 p[3]={float2(-1,-1),float2(3,-1),float2(-1,3)}; return float4(p[v],0,1); }"
    "fragment float4 fs(constant U& u [[buffer(2)]]) {return u.values[149];}";
static const char* vertex_source = "#include <metal_stdlib>\nusing namespace metal;\n"
    "struct U { float4 values[150]; }; struct V { float4 p [[position]]; float4 color [[flat]]; };"
    "vertex V vs(uint v [[vertex_id]], constant U& u [[buffer(0)]]) { float2 p[3]={float2(-1,-1),float2(3,-1),float2(-1,3)}; V o; o.p=float4(p[v],0,1); o.color=u.values[149]; return o; }"
    "fragment float4 fs(V v [[stage_in]]) {return v.color;}";
#else
static const char* fragment_source = "struct U {values:array<vec4f,150>}; @group(0) @binding(3) var<uniform> u:U;"
    "@vertex fn vs(@builtin(vertex_index) v:u32)->@builtin(position) vec4f {return vec4f(select(-1.0,3.0,v==1),select(-1.0,3.0,v==2),0,1);}"
    "@fragment fn fs()->@location(0) vec4f {return u.values[149];}";
static const char* vertex_source = "struct U {values:array<vec4f,150>}; @group(0) @binding(1) var<uniform> u:U;"
    "struct V {@builtin(position) p:vec4f, @location(0) @interpolate(flat) color:vec4f};"
    "@vertex fn vs(@builtin(vertex_index) v:u32)->V {var o:V; o.p=vec4f(select(-1.0,3.0,v==1),select(-1.0,3.0,v==2),0,1); o.color=u.values[149]; return o;}"
    "@fragment fn fs(v:V)->@location(0) vec4f {return v.color;}";
#endif
EXPORT void probe_init(void) {
    sg_desc setup = {.logger.func = logger, .environment.defaults = {
        .color_format = SG_PIXELFORMAT_RGBA8, .depth_format = SG_PIXELFORMAT_NONE, .sample_count = 1}};
#if defined(SOKOL_METAL)
    setup.environment.metal.device = (__bridge const void*)MTLCreateSystemDefaultDevice();
#else
    setup.environment.wgpu.device = emscripten_webgpu_get_device();
#endif
    sg_setup(&setup); CHECK(sg_isvalid());
    sg_install_trace_hooks(&(sg_trace_hooks){.apply_uniforms = trace_uniform});
    for (int i = 0; i < 3; i++) {
        const char* src = i == 2 ? vertex_source : fragment_source;
        sg_shader_desc desc = {.vertex_func = {.source = src, .entry = "vs"}, .fragment_func = {.source = src, .entry = "fs"}};
        const int slot = i == 2 ? 0 : 3;
        desc.uniform_blocks[slot] = (sg_shader_uniform_block){.stage = i == 2 ? SG_SHADERSTAGE_VERTEX : SG_SHADERSTAGE_FRAGMENT,
            .size = sizeof red, .msl_buffer_n = i == 2 ? 0 : 2, .wgsl_group0_binding_n = i == 2 ? 1 : 3};
        shaders[i] = sg_make_shader(&desc); CHECK(sg_query_shader_state(shaders[i]) == SG_RESOURCESTATE_VALID);
    }
    const int shader_indices[4] = {0, 0, 1, 2};
    for (int i = 0; i < 4; i++) {
        pipelines[i] = sg_make_pipeline(&(sg_pipeline_desc){.shader = shaders[shader_indices[i]], .sample_count = 1});
        CHECK(sg_query_pipeline_state(pipelines[i]) == SG_RESOURCESTATE_VALID);
    }
    image = sg_make_image(&(sg_image_desc){.width = 8, .height = 1, .pixel_format = SG_PIXELFORMAT_RGBA8,
        .usage = {.color_attachment = true, .copy_src = true}});
    color = sg_make_view(&(sg_view_desc){.color_attachment.image = image});
    CHECK(sg_query_view_state(color) == SG_RESOURCESTATE_VALID);
    red[596] = red[599] = green[597] = green[599] = 1;
    red_id = sg_alloc_uniform_data_id(); green_id = sg_alloc_uniform_data_id(); CHECK(red_id && green_id > red_id);
}
EXPORT void probe_render(void) {
    const unsigned before = traces;
    for (int pass = 0; pass < 2; pass++) {
        sg_pass target = {.attachments.colors[0] = color};
        if (pass) {target.action.colors[0].load_action = SG_LOADACTION_LOAD; sg_reset_state_cache();}
        sg_begin_pass(&target);
        for (int x = pass*4; x < pass*4+4; x++) {
            const int pipeline_indices[8] = {0, 1, 2, 0, 0, 3, 0, 2};
            sg_apply_pipeline(pipelines[pipeline_indices[x]]); sg_apply_scissor_rect(x, 0, 1, 1, true);
            int slot = x == 5 ? 0 : 3;
            if (x == 6) sg_apply_uniforms(slot, &SG_RANGE(red));
            else sg_apply_uniforms_cached(slot, x == 3 ? &SG_RANGE(green) : &SG_RANGE(red), x == 3 ? green_id : red_id);
            sg_draw(0, 3, 1);
        }
        sg_end_pass();
    }
    CHECK(traces == before+8);
#if defined(SOKOL_METAL)
    CHECK(_sg.mtl.cur_ub_offset == 3*_sg_roundup_pow2(sizeof red, _SG_MTL_UB_ALIGN));
#else
    CHECK(_sg.wgpu.uniform.offset == 3*_sg_roundup_pow2_u32(sizeof red, _sg.wgpu.limits.minUniformBufferOffsetAlignment));
#endif
#if defined(SOKOL_METAL)
    frame_command = (__bridge id<MTLCommandBuffer>)sg_mtl_command_buffer();
#if !__has_feature(objc_arc)
    [frame_command retain];
#endif
#endif
    sg_commit();
    sg_frame_stats stats = sg_query_stats().prev_frame;
    if (sg_stats_enabled()) {
        CHECK(stats.num_apply_uniforms == 8 && stats.num_apply_uniforms_cached == 7);
        CHECK(stats.num_reuse_uniforms == 5 && stats.size_reuse_uniforms == 5*sizeof red);
#if defined(SOKOL_METAL)
        CHECK(stats.metal.uniforms.size_used == 3u*(uint32_t)_sg_roundup_pow2(sizeof red, _SG_MTL_UB_ALIGN));
#else
        CHECK(stats.wgpu.uniforms.size_copy == 3*sizeof red && stats.wgpu.uniforms.num_set_bindgroup == 8);
        CHECK(stats.wgpu.uniforms.size_write_buffer == 3*_sg_roundup_pow2_u32(sizeof red, _sg.wgpu.limits.minUniformBufferOffsetAlignment));
#endif
    } else CHECK(!stats.num_apply_uniforms && !stats.wgpu.uniforms.size_hash);
#if defined(SOKOL_WGPU)
    const bool profiled = sg_stats_enabled() && _sg.wgpu.uniform.profiled;
    CHECK(stats.wgpu.uniforms.size_hash == (profiled ? 3*sizeof red : 0));
    CHECK(stats.wgpu.uniforms.num_unique == (profiled ? 2u : 0u) && stats.wgpu.uniforms.size_unique == (profiled ? 2*sizeof red : 0));
    CHECK(profiled ? stats.wgpu.uniforms.size_compare > 0 : !stats.wgpu.uniforms.size_compare);
    CHECK(!_sg.wgpu.uniform.num_records);
    CHECK(!_sg.wgpu.uniform.profiled || _sg.wgpu.uniform.records || !sg_stats_enabled());
#else
    CHECK(!sg_query_stats().wgpu_memory.uniform_gpu_bytes);
#endif
}
EXPORT void probe_shutdown(void) {
    sg_destroy_view(color); sg_destroy_image(image);
    for (int i = 0; i < 4; i++) sg_destroy_pipeline(pipelines[i]);
    for (int i = 0; i < 3; i++) sg_destroy_shader(shaders[i]);
    sg_shutdown();
}
#if defined(SOKOL_WGPU)
EXPORT double probe_native_count(int key) {
    const sg_frame_stats_wgpu s = sg_query_stats().prev_frame.wgpu;
    const double values[] = {s.num_begin_render_pass, s.num_begin_compute_pass, s.num_set_pipeline,
        s.uniforms.num_set_bindgroup, s.bindings.num_set_bindgroup, s.transfers.num_write_buffer,
        (double)s.transfers.size_write_buffer, s.transfers.num_queue_submit, s.bindings.num_create_bindgroup,
        s.transfers.num_write_texture, (double)s.transfers.size_write_texture,
        s.transfers.num_copy_buffer, (double)s.transfers.size_copy_buffer};
    CHECK(key >= 0 && key < (int)(sizeof values/sizeof *values)); return values[key];
}
static sg_buffer transfer_buffers[4];
static sg_image transfer_image;
EXPORT void probe_transfers(void) {
    const uint8_t bytes[20] = {1,2,3,4,5,6,7,0,9,10,11,12,13,14,15,16,17,18,19,20};
    transfer_buffers[0] = sg_make_buffer(&(sg_buffer_desc){.size = 8, .usage = {.index_buffer = true, .write_transient = true}});
    transfer_buffers[1] = sg_make_buffer(&(sg_buffer_desc){.size = 8, .usage = {.staging_index_buffer = true, .copy_src = true, .write_transient = true}});
    transfer_buffers[2] = sg_make_buffer(&(sg_buffer_desc){.size = 8, .usage = {.index_buffer = true, .copy_dst = true, .copy_src = true}});
    transfer_buffers[3] = sg_make_buffer(&(sg_buffer_desc){.size = 8, .usage = {.index_buffer = true, .copy_dst = true, .copy_src = true}});
    sg_write_buffer_transient(&(sg_write_buffer_desc){.dst.buffer = transfer_buffers[0], .src.data = {bytes,7}});
    sg_write_buffer_transient(&(sg_write_buffer_desc){.dst.buffer = transfer_buffers[1], .src.data = {bytes,7}});
    sg_copy_buffer_to_buffer(&(sg_copy_buffer_to_buffer_desc){.src.buffer = transfer_buffers[1], .dst.buffer = transfer_buffers[2], .size = 7});
    sg_copy_buffer_to_buffer(&(sg_copy_buffer_to_buffer_desc){.src.buffer = transfer_buffers[2], .dst.buffer = transfer_buffers[3], .size = 7});
    transfer_image = sg_make_image(&(sg_image_desc){.width = 2, .height = 2, .num_mipmaps = 2, .usage.copy_src = true, .data = SG_RANGE(bytes)});
    sg_commit();
    const sg_frame_stats_transfers s = sg_query_stats().prev_frame.wgpu.transfers;
    CHECK(s.size_memcpy == 13 && s.size_write_padding == 2 && s.size_write_buffer == 16 && s.size_write_texture == 40 && s.size_copy_buffer == 8);
}
EXPORT uintptr_t probe_transfer_buffer(void) {return (uintptr_t)sg_wgpu_query_buffer_info(transfer_buffers[3]).buf;}
EXPORT uintptr_t probe_transfer_image(void) {return (uintptr_t)sg_wgpu_query_image_info(transfer_image).tex;}
EXPORT void probe_transfer_cleanup(void) {
    for (int i = 0; i < 4; i++) sg_destroy_buffer(transfer_buffers[i]); sg_destroy_image(transfer_image);
}
EXPORT void probe_bad_layout(void) {
    sg_shader shader = sg_make_shader(&(sg_shader_desc){
        .vertex_func = {.source = fragment_source, .entry = "vs"}, .fragment_func = {.source = fragment_source, .entry = "fs"},
        .uniform_blocks[3] = {.stage = SG_SHADERSTAGE_FRAGMENT, .size = 16, .wgsl_group0_binding_n = 3}});
    sg_pipeline pipeline = sg_make_pipeline(&(sg_pipeline_desc){.shader = shader, .sample_count = 1});
    sg_destroy_pipeline(pipeline); sg_destroy_shader(shader);
}
EXPORT void probe_resources(int disabled) {
    _sg.desc.wgpu.disable_bindgroups_cache = disabled != 0;
    const char* vertex = "@vertex fn vs(@builtin(vertex_index) v:u32)->@builtin(position) vec4f {return vec4f(select(-1.0,3.0,v==1),select(-1.0,3.0,v==2),0,1);}";
    const char* fragment = "@group(1) @binding(7) var<storage,read> color:vec4f; @fragment fn fs()->@location(0) vec4f {return color;}";
    sg_shader resource = sg_make_shader(&(sg_shader_desc){.vertex_func = {.source = vertex, .entry = "vs"},
        .fragment_func = {.source = fragment, .entry = "fs"},
        .views[2].storage_buffer = {.stage = SG_SHADERSTAGE_FRAGMENT, .readonly = true, .wgsl_group1_binding_n = 7}});
    sg_shader empty = sg_make_shader(&(sg_shader_desc){.vertex_func = {.source = vertex, .entry = "vs"},
        .fragment_func = {.source = "@fragment fn fs()->@location(0) vec4f {return vec4f(0,0,1,1);}", .entry = "fs"}});
    sg_shader compute = sg_make_shader(&(sg_shader_desc){.compute_func.source = "@compute @workgroup_size(1) fn main() {}"});
    CHECK(sg_query_shader_state(resource) == SG_RESOURCESTATE_VALID && sg_query_shader_state(empty) == SG_RESOURCESTATE_VALID);
    sg_pipeline render[3];
    for (int i = 0; i < 3; i++) {
        render[i] = sg_make_pipeline(&(sg_pipeline_desc){.shader = i == 2 ? empty : resource, .sample_count = 1,
            .face_winding = i == 1 ? SG_FACEWINDING_CCW : SG_FACEWINDING_CW});
        CHECK(sg_query_pipeline_state(render[i]) == SG_RESOURCESTATE_VALID);
    }
    sg_pipeline cp = sg_make_pipeline(&(sg_pipeline_desc){.compute = true, .shader = compute});
    CHECK(sg_query_pipeline_state(cp) == SG_RESOURCESTATE_VALID);
    sg_buffer buffers[2]; sg_view views[2];
    for (int i = 0; i < 2; i++) {
        buffers[i] = sg_make_buffer(&(sg_buffer_desc){.usage.storage_buffer = true,
            .data = {.ptr = i ? &green[596] : &red[596], .size = 16}});
        views[i] = sg_make_view(&(sg_view_desc){.storage_buffer.buffer = buffers[i]});
        CHECK(sg_query_view_state(views[i]) == SG_RESOURCESTATE_VALID);
    }
    sg_begin_pass(&(sg_pass){.compute = true}); sg_apply_pipeline(cp); sg_dispatch(1,1,1); sg_end_pass();
    sg_begin_pass(&(sg_pass){.attachments.colors[0] = color});
    uint32_t creates = _sg.stats.cur_frame.wgpu.bindings.num_create_bindgroup;
    for (int x = 0; x < 8; x++) {
        sg_apply_pipeline(render[x == 2 ? 2 : x == 1 ? 1 : 0]); sg_apply_scissor_rect(x,0,1,1,true);
        if (x != 2) sg_apply_bindings(&(sg_bindings){.views[2] = views[x == 4 || x == 6]});
        sg_draw(0,3,1);
        if (x == 1 && !disabled) CHECK(_sg.stats.cur_frame.wgpu.bindings.num_create_bindgroup == creates+1);
    }
    for (int i = 0; i < 2; i++) {sg_destroy_buffer(buffers[i]); sg_destroy_view(views[i]);}
    sg_end_pass(); sg_commit();
    for (int i = 0; i < 3; i++) sg_destroy_pipeline(render[i]);
    sg_destroy_pipeline(cp); sg_destroy_shader(resource); sg_destroy_shader(empty); sg_destroy_shader(compute);
}
EXPORT uintptr_t probe_texture(void) { return (uintptr_t)sg_wgpu_query_image_info(image).tex; }
#else
int main(void) {
    @autoreleasepool {
        probe_init();
        id<MTLDevice> device = (__bridge id<MTLDevice>)sg_query_desc().environment.metal.device;
        unsigned pixels = 0;
        for (int frame = 0; frame < SG_NUM_INFLIGHT_FRAMES+2; frame++) {
            probe_profile(frame);
            probe_render(); [frame_command waitUntilCompleted]; CHECK(frame_command.status == MTLCommandBufferStatusCompleted);
#if !__has_feature(objc_arc)
            [frame_command release];
#endif
            frame_command = nil;
            id<MTLCommandQueue> queue = [device newCommandQueue];
            id<MTLCommandBuffer> cmd = [queue commandBuffer];
            id<MTLBuffer> read = [device newBufferWithLength:256 options:MTLResourceStorageModeShared]; CHECK(read && cmd);
            id<MTLBlitCommandEncoder> blit = [cmd blitCommandEncoder]; CHECK(blit);
            [blit copyFromTexture:(__bridge id<MTLTexture>)sg_mtl_query_image_info(image).tex[0]
                sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(8,1,1)
                toBuffer:read destinationOffset:0 destinationBytesPerRow:256 destinationBytesPerImage:256];
            [blit endEncoding]; [cmd commit]; [cmd waitUntilCompleted]; CHECK(cmd.status == MTLCommandBufferStatusCompleted);
            const uint8_t* bytes = read.contents;
            for (int x = 0; x < 8; x++) {CHECK(bytes[x*4] == (x==3?0:255) && bytes[x*4+1] == (x==3?255:0) && bytes[x*4+2] == 0 && bytes[x*4+3] == 255); pixels++;}
#if !__has_feature(objc_arc)
            [read release]; [queue release];
#endif
        }
        const uint64_t previous_id = green_id; probe_shutdown(); probe_init(); CHECK(red_id > previous_id); probe_shutdown();
        printf("uniform IDs: %u exact Metal pixels, five reuses/frame\n", pixels);
    }
}
#endif
