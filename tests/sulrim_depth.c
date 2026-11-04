// LLM maintained. Actual offscreen depth rendering; native and Wasm share the workload.
#define SOKOL_IMPL
#define SOKOL_VALIDATE_NON_FATAL
#include "sokol_gfx.h"
#include <stdio.h>
#include <stdlib.h>
#if defined(SOKOL_WGPU)
#include <emscripten.h>
extern WGPUDevice emscripten_webgpu_get_device(void);
#define EXPORT EMSCRIPTEN_KEEPALIVE
#else
#include <unistd.h>
#define EXPORT
#endif
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); abort(); } } while (0)

static sg_shader shaded, depth_shaders[2];
static sg_pipeline depth_pipelines[2];
static sg_pipeline pipelines[2];
static sg_image color;
static int width, height, validation_errors;
static bool expect_error;
#if defined(SOKOL_METAL)
static uint64_t pixels_checked;
#endif

static void logger(const char* tag, uint32_t level, uint32_t item, const char* message, uint32_t line, const char* file, void* data) {
    (void)tag; (void)item; (void)file; (void)data;
    if (level <= 1 && expect_error) { validation_errors++; return; }
    if (level <= 1) { fprintf(stderr, "%u: %s\n", line, message ? message : "error"); abort(); }
}
#include "sulrim_pass_local_depth.h"
#if defined(SOKOL_METAL)
static const char* source = "#include <metal_stdlib>\nusing namespace metal;\n"
    "struct V { float4 p [[position, invariant]]; float4 color; };"
    "vertex V vs(uint i [[vertex_id]]) { uint v=i%3; V o; bool far=i>=3;"
    "o.p=float4(v==1?3.0:-1.0,v==2?3.0:-1.0,far?0.75:0.25,1);"
    "o.color=far?float4(0,1,0,1):float4(1,0,0,1);return o;}"
    "fragment float4 fs(V v [[stage_in]]) {return v.color;}"
    "fragment void fade(V v [[stage_in]]) {if((uint(v.p.x)&1u)==0u) discard_fragment();}";
#else
static const char* source = "struct V { @builtin(position) @invariant p:vec4f, @location(0) color:vec4f };"
    "@vertex fn vs(@builtin(vertex_index) i:u32)->V { let v=i%3; var o:V; let far=i>=3;"
    "o.p=vec4f(select(-1.0,3.0,v==1),select(-1.0,3.0,v==2),select(0.25,0.75,far),1);"
    "o.color=select(vec4f(1,0,0,1),vec4f(0,1,0,1),far);return o;}"
    "@fragment fn fs(v:V)->@location(0) vec4f {return v.color;}"
    "@fragment fn fade(v:V) {if((u32(v.p.x)&1u)==0u) {discard;}}";
#endif

EXPORT void probe_init(void) {
    sg_desc desc = {.logger.func = logger, .environment.defaults = {
        .color_format = SG_PIXELFORMAT_RGBA8, .depth_format = SG_PIXELFORMAT_NONE, .sample_count = 1}};
#if defined(SOKOL_METAL)
    desc.environment.metal.device = (__bridge const void*)MTLCreateSystemDefaultDevice();
#else
    desc.environment.wgpu.device = emscripten_webgpu_get_device();
#endif
    sg_setup(&desc);
    CHECK(sg_isvalid());
    check_pass_local_depth_contract();
    _sg.desc.disable_validation = true; check_pass_local_depth_contract(); _sg.desc.disable_validation = false;
    sg_pixelformat_info format = sg_query_pixelformat(SG_PIXELFORMAT_DEPTH16);
    CHECK(format.depth && format.sample && format.render && format.msaa && format.bytes_per_pixel == 2 && !format.compressed);
    CHECK(sg_query_row_pitch(SG_PIXELFORMAT_DEPTH16, 17, 1) == 34);
    CHECK(sg_query_surface_pitch(SG_PIXELFORMAT_DEPTH16, 17, 3, 1) == 102);
    shaded = sg_make_shader(&(sg_shader_desc){
        .vertex_func = {.source = source, .entry = "vs"}, .fragment_func = {.source = source, .entry = "fs"}});
    CHECK(sg_query_shader_state(shaded) == SG_RESOURCESTATE_VALID);
    for (int i = 0; i < 2; i++) {
        pipelines[i] = sg_make_pipeline(&(sg_pipeline_desc){.shader = shaded, .sample_count = 1,
            .colors[0].pixel_format = SG_PIXELFORMAT_RGBA8,
            .depth = {.pixel_format = i ? SG_PIXELFORMAT_DEPTH16 : SG_PIXELFORMAT_DEPTH,
                .compare = SG_COMPAREFUNC_LESS, .write_enabled = true}});
        CHECK(sg_query_pipeline_state(pipelines[i]) == SG_RESOURCESTATE_VALID);
        depth_shaders[i] = sg_make_shader(&(sg_shader_desc){.vertex_func = {.source = source, .entry = "vs"},
            .fragment_func = {.source = i ? source : 0, .entry = "fade"}});
        CHECK(sg_query_shader_state(depth_shaders[i]) == SG_RESOURCESTATE_VALID);
#if defined(SOKOL_METAL)
        CHECK((sg_mtl_query_shader_info(depth_shaders[i]).fragment_func != 0) == (i != 0));
#endif
        depth_pipelines[i] = sg_make_pipeline(&(sg_pipeline_desc){.shader = depth_shaders[i], .sample_count = 1,
            .colors[0].pixel_format = SG_PIXELFORMAT_NONE,
            .depth = {.pixel_format = SG_PIXELFORMAT_DEPTH, .compare = SG_COMPAREFUNC_LESS, .write_enabled = true}});
        CHECK(sg_query_pipeline_state(depth_pipelines[i]) == SG_RESOURCESTATE_VALID);
    }
}

EXPORT void probe_render(int mode, int w, int h) {
    CHECK(mode >= 0 && mode < 6 && !color.id);
    width = w; height = h;
    sg_image depth = sg_make_image(&(sg_image_desc){.width = width, .height = height, .sample_count = 1,
        .label = "sulrim-depth",
        .pixel_format = mode == 1 ? SG_PIXELFORMAT_DEPTH16 : SG_PIXELFORMAT_DEPTH,
        .usage = {.depth_stencil_attachment = true, .pass_local_depth = mode == 4}});
    CHECK(sg_query_image_state(depth) == SG_RESOURCESTATE_VALID);
#if defined(SOKOL_METAL)
    sg_mtl_image_info info = sg_mtl_query_image_info(depth);
    id<MTLTexture> native = (__bridge id<MTLTexture>)info.tex[0];
    CHECK(native.pixelFormat == (mode == 1 ? MTLPixelFormatDepth16Unorm : MTLPixelFormatDepth32Float));
    bool memoryless = mode == 4 && [_sg.mtl.device supportsFamily:MTLGPUFamilyApple1];
    CHECK(native.storageMode == (memoryless ? MTLStorageModeMemoryless : MTLStorageModePrivate));
    CHECK(native.usage == (mode == 4 ? MTLTextureUsageRenderTarget : MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead));
    if (memoryless) CHECK(native.allocatedSize == 0);
#endif
    color = sg_make_image(&(sg_image_desc){.width = width, .height = height, .sample_count = 1,
        .pixel_format = SG_PIXELFORMAT_RGBA8, .usage = {.color_attachment = true, .copy_src = true}});
    sg_view cv = sg_make_view(&(sg_view_desc){.color_attachment.image = color});
    sg_view dv = sg_make_view(&(sg_view_desc){.depth_stencil_attachment.image = depth});
    CHECK(sg_query_image_state(color) == SG_RESOURCESTATE_VALID);
    CHECK(sg_query_view_state(cv) == SG_RESOURCESTATE_VALID && sg_query_view_state(dv) == SG_RESOURCESTATE_VALID);
    for (int frame = 0; frame < SG_NUM_INFLIGHT_FRAMES + 2; frame++) {
        if (mode == 2 || mode == 3 || mode == 5) {
            sg_begin_pass(&(sg_pass){.attachments.depth_stencil = dv,
                .action.depth = {.load_action = SG_LOADACTION_CLEAR, .clear_value = 1, .store_action = SG_STOREACTION_STORE}});
            sg_apply_pipeline(depth_pipelines[mode == 3]); sg_draw(0, 3, 1); sg_end_pass();
            sg_begin_pass(&(sg_pass){.attachments = {.colors[0] = cv, .depth_stencil = dv},
                .action = {.colors[0] = {.load_action = SG_LOADACTION_CLEAR, .clear_value = {0,0,0,1}},
                    .depth.load_action = SG_LOADACTION_LOAD}});
            sg_apply_pipeline(pipelines[0]); sg_draw(mode == 5 ? 0 : 3, 3, 1); sg_end_pass();
        } else {
            for (int slice = 0; slice < 4; slice++) {
                sg_begin_pass(&(sg_pass){.attachments = {.colors[0] = cv, .depth_stencil = dv}, .action = {
                    .colors[0] = {.load_action = slice ? SG_LOADACTION_LOAD : SG_LOADACTION_CLEAR, .clear_value = {0,0,0,1}},
                    .depth = {.load_action = SG_LOADACTION_CLEAR, .clear_value = 1}}});
                sg_apply_pipeline(pipelines[mode == 4 ? 0 : mode]);
                sg_apply_viewport(slice * width / 4, 0, width / 4, height, true);
                if (!(slice & 1)) sg_draw(0, 3, 1);
                sg_draw(3, 3, 1);
                sg_end_pass();
            }
        }
        sg_commit();
    }
    sg_destroy_view(cv); sg_destroy_view(dv); sg_destroy_image(depth);
}
EXPORT void probe_release(void) { sg_destroy_image(color); color.id = 0; }
EXPORT void probe_shutdown(void) { sg_shutdown(); }
#if defined(SOKOL_WGPU)
EXPORT WGPUTexture probe_texture(void) { return (WGPUTexture)sg_wgpu_query_image_info(color).tex; }
#else
static void check_render(int mode) {
    id<MTLTexture> texture = (__bridge id<MTLTexture>)sg_mtl_query_image_info(color).tex[0];
    size_t pitch = sg_query_row_pitch(SG_PIXELFORMAT_RGBA8, width, 256), bytes = pitch * height;
    id<MTLBuffer> read = [_sg.mtl.device newBufferWithLength:bytes options:MTLResourceStorageModeShared];
    CHECK(read);
    id<MTLCommandBuffer> cmd = [_sg.mtl.cmd_queue commandBuffer];
    id<MTLBlitCommandEncoder> blit = [cmd blitCommandEncoder];
    [blit copyFromTexture:texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
        sourceSize:MTLSizeMake(width,height,1) toBuffer:read destinationOffset:0
        destinationBytesPerRow:pitch destinationBytesPerImage:bytes];
    [blit endEncoding]; [cmd commit]; [cmd waitUntilCompleted];
    CHECK(cmd.status == MTLCommandBufferStatusCompleted);
    const uint8_t* pixels = read.contents;
    for (int y = 0; y < height; y++) for (int x = 0; x < width; x++) {
        const uint8_t* p = pixels + y * pitch + x * 4;
        bool green = mode == 5 ? false : mode == 2 || mode == 3 ? mode == 3 && !(x & 1) : (x / (width / 4)) & 1;
        CHECK(p[0] == ((mode < 2 || mode >= 4) && !green ? 255 : 0) && p[1] == (green ? 255 : 0) && p[2] == 0 && p[3] == 255);
        pixels_checked++;
    }
    _SG_OBJC_RELEASE(read);
}
static void check_invariance(void) {
    CHECK(_sg.mtl.compile_options.preserveInvariance);
    const sg_pipeline saved_depth = depth_pipelines[0], saved_color = pipelines[0];
    for (int async = 0; async < 2; async++) {
        sg_shader_desc desc = {.vertex_func = {.source = source, .entry = "vs"}, .async_compile = async};
        sg_shader pre = sg_make_shader(&desc);
        desc.fragment_func = (sg_shader_function){.source = source, .entry = "fs"};
        sg_shader shaded = sg_make_shader(&desc);
        depth_pipelines[0] = sg_make_pipeline(&(sg_pipeline_desc){.shader = pre, .async_compile = async,
            .colors[0].pixel_format = SG_PIXELFORMAT_NONE,
            .depth = {.pixel_format = SG_PIXELFORMAT_DEPTH, .compare = SG_COMPAREFUNC_LESS, .write_enabled = true}});
        pipelines[0] = sg_make_pipeline(&(sg_pipeline_desc){.shader = shaded, .async_compile = async,
            .depth = {.pixel_format = SG_PIXELFORMAT_DEPTH, .compare = SG_COMPAREFUNC_EQUAL}});
        if (async) CHECK(sg_query_pipeline_state(depth_pipelines[0]) == SG_RESOURCESTATE_PENDING);
        int frames = 0;
        while (sg_query_pipeline_state(depth_pipelines[0]) == SG_RESOURCESTATE_PENDING || sg_query_pipeline_state(pipelines[0]) == SG_RESOURCESTATE_PENDING) {
            CHECK(++frames < 4000); sg_commit(); usleep(1000);
        }
        CHECK(sg_query_pipeline_state(depth_pipelines[0]) == SG_RESOURCESTATE_VALID && sg_query_pipeline_state(pipelines[0]) == SG_RESOURCESTATE_VALID);
        probe_render(5, 128, 64); check_render(5); probe_release();
        sg_destroy_pipeline(depth_pipelines[0]); sg_destroy_pipeline(pipelines[0]);
        sg_destroy_shader(pre); sg_destroy_shader(shaded);
    }
    depth_pipelines[0] = saved_depth; pipelines[0] = saved_color;
}
int main(void) {
    @autoreleasepool {
        probe_init();
#if !defined(SULRIM_INVARIANCE_ONLY)
        const int sizes[][2] = {{16,4},{32,8},{128,64},{64,32}};
        for (int i = 0; i < 4; i++) for (int mode = 0; mode < 5; mode++) {
            probe_render(mode, sizes[i][0], sizes[i][1]); check_render(mode); probe_release();
        }
#endif
        check_invariance();
        probe_shutdown();
        printf("Metal depth: %llu exact pixels, sync/async invariant EQUAL passes PASS\n", (unsigned long long)pixels_checked);
    }
}
#endif
