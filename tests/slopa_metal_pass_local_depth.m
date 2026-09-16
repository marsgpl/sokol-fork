// Headless Metal: pass-local depth allocation, clear/reuse, occlusion and resize.
#define SOKOL_IMPL
#define SOKOL_METAL
#include "sokol_gfx.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include "slopa_pass_local_depth.h"

static void check_render(id<MTLDevice> device, sg_pipeline pipeline, int width, int height, bool is_local) {
    NSUInteger before = device.currentAllocatedSize;
    sg_image depth = sg_make_image(&(sg_image_desc){
        .width = width, .height = height, .pixel_format = SG_PIXELFORMAT_DEPTH, .sample_count = 1,
        .usage = {.depth_stencil_attachment = true, .pass_local_depth = is_local},
    });
    assert(sg_query_image_state(depth) == SG_RESOURCESTATE_VALID);
    sg_mtl_image_info info = sg_mtl_query_image_info(depth);
    id<MTLTexture> native = (__bridge id<MTLTexture>)info.tex[0];
    assert(!info.tex[1]);
    bool is_memoryless = is_local && [device supportsFamily:MTLGPUFamilyApple1];
    assert(native.storageMode == (is_memoryless ? MTLStorageModeMemoryless : MTLStorageModePrivate));
    assert(native.usage == (is_local ? MTLTextureUsageRenderTarget : MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead));
    if (is_memoryless) assert(native.allocatedSize == 0);
    printf("%s %dx%d pass_local=%d allocated=%lu device_delta=%lu\n",
        device.name.UTF8String, width, height, is_local, native.allocatedSize, device.currentAllocatedSize - before);
    sg_image color = sg_make_image(&(sg_image_desc){.width = width, .height = height,
        .pixel_format = SG_PIXELFORMAT_RGBA8, .sample_count = 1, .usage.color_attachment = true});
    sg_view color_view = sg_make_view(&(sg_view_desc){.color_attachment.image = color});
    sg_view depth_view = sg_make_view(&(sg_view_desc){.depth_stencil_attachment.image = depth});
    assert(sg_query_image_state(color) == SG_RESOURCESTATE_VALID);
    assert(sg_query_view_state(color_view) == SG_RESOURCESTATE_VALID && sg_query_view_state(depth_view) == SG_RESOURCESTATE_VALID);
    size_t pitch = _sg_roundup(width * 4, 256), bytes = pitch * height;
    id<MTLBuffer> readback = [device newBufferWithLength:bytes options:MTLResourceStorageModeShared];
    assert(readback);
    for (int frame = 0; frame < SG_NUM_INFLIGHT_FRAMES + 2; frame++) {
        for (int slice = 0; slice < 4; slice++) {
            sg_begin_pass(&(sg_pass){
                .attachments = {.colors[0] = color_view, .depth_stencil = depth_view},
                .action = {.colors[0].load_action = slice ? SG_LOADACTION_LOAD : SG_LOADACTION_CLEAR,
                    .depth = {.load_action = SG_LOADACTION_CLEAR, .clear_value = 1}},
            });
            sg_apply_pipeline(pipeline);
            sg_apply_viewport(slice * width / 4, 0, width / 4, height, true);
            if (!(slice & 1)) sg_draw(0, 3, 1); // Near red occludes the later far green.
            sg_draw(3, 3, 1); // Next slice must accept far geometry after depth clear.
            sg_end_pass();
        }
        id<MTLCommandBuffer> command = [(__bridge id<MTLCommandBuffer>)sg_mtl_command_buffer() retain];
        id<MTLTexture> texture = (__bridge id<MTLTexture>)sg_mtl_query_image_info(color).tex[0];
        id<MTLBlitCommandEncoder> blit = [command blitCommandEncoder];
        assert(blit);
        [blit copyFromTexture:texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
            sourceSize:MTLSizeMake(width,height,1) toBuffer:readback destinationOffset:0
            destinationBytesPerRow:pitch destinationBytesPerImage:bytes];
        [blit endEncoding];
        sg_commit();
        [command waitUntilCompleted]; // Standalone test only.
        assert(command.status == MTLCommandBufferStatusCompleted);
        const uint8_t* pixels = readback.contents;
        for (int y = 0; y < height; y++) for (int x = 0; x < width; x++) {
            const uint8_t* p = pixels + y * pitch + x * 4;
            bool is_green = (x / (width / 4)) & 1;
            assert(p[0] == (is_green ? 0 : 255) && p[1] == (is_green ? 255 : 0) && p[2] == 0 && p[3] == 255);
        }
        [command release];
    }
    [readback release];
    sg_destroy_view(color_view); sg_destroy_view(depth_view);
    sg_destroy_image(color); sg_destroy_image(depth);
}

int main(void) {
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        assert(device);
        sg_setup(&(sg_desc){.environment.metal.device = device});
        check_pass_local_depth_contract();
        _sg.desc.disable_validation = true;
        check_pass_local_depth_contract();
        _sg.desc.disable_validation = false;
        const char* source = "#include <metal_stdlib>\nusing namespace metal;\n"
            "struct V { float4 p [[position]]; float4 color; };\n"
            "vertex V vs(uint i [[vertex_id]]) { uint v=i%3; V o; bool far=i>=3;"
            "o.p=float4(v==1?3.0:-1.0,v==2?3.0:-1.0,far?0.75:0.25,1);"
            "o.color=far?float4(0,1,0,1):float4(1,0,0,1);return o;}\n"
            "fragment float4 fs(V v [[stage_in]]) {return v.color;}\n";
        sg_shader shader = sg_make_shader(&(sg_shader_desc){
            .vertex_func = {.source = source, .entry = "vs"}, .fragment_func = {.source = source, .entry = "fs"}});
        sg_pipeline pipeline = sg_make_pipeline(&(sg_pipeline_desc){.shader = shader, .sample_count = 1,
            .colors[0].pixel_format = SG_PIXELFORMAT_RGBA8,
            .depth = {.pixel_format = SG_PIXELFORMAT_DEPTH, .compare = SG_COMPAREFUNC_LESS, .write_enabled = true}});
        assert(sg_query_shader_state(shader) == SG_RESOURCESTATE_VALID && sg_query_pipeline_state(pipeline) == SG_RESOURCESTATE_VALID);
        const int sizes[][2] = {{16,4},{32,8},{2048,1024},{1024,512}};
        for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
            check_render(device, pipeline, sizes[i][0], sizes[i][1], false);
            check_render(device, pipeline, sizes[i][0], sizes[i][1], true);
        }
        sg_destroy_pipeline(pipeline); sg_destroy_shader(shader);
        sg_shutdown(); [device release];
    }
    puts("Metal pass-local depth: exact expected color, occlusion, clear/reuse, resize and contract checks passed");
}
