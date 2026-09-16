// Headless real Metal: immutable content IDs, shader switches and ring rotation.
#define SOKOL_IMPL
#define SOKOL_METAL
#include "sokol_gfx.h"
#include <stdio.h>
#include <time.h>

static void logger(const char* tag, uint32_t level, uint32_t item, const char* message, uint32_t line, const char* file, void* user) {
    (void)tag; (void)level; (void)item; (void)line; (void)file; (void)user;
    fprintf(stderr, "%s\n", message ? message : "Sokol error");
}

static uint64_t now_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}

static void benchmark(sg_view color, sg_pipeline pipeline) {
    sg_disable_stats();
    float data[600] = {0};
    uint64_t data_id = sg_alloc_uniform_data_id();
    const int counts[] = {16, 1024};
    for (int n = 0; n < 2; ++n) {
        uint64_t elapsed[2] = {0};
        for (int f = 0; f < 220; ++f) {
            sg_begin_pass(&(sg_pass){.attachments.colors[0] = color});
            sg_apply_pipeline(pipeline);
            const uint64_t start = now_ns();
            for (int i = 0; i < counts[n]; ++i) {
                if (f % 2) sg_apply_uniforms_cached(3, &SG_RANGE(data), data_id);
                else sg_apply_uniforms(3, &SG_RANGE(data));
            }
            if (f >= 20) elapsed[f % 2] += now_ns() - start;
            sg_end_pass();
            sg_commit();
        }
        printf("Metal CPU encoding: bytes=2400 applies/commit=%d identical ordinary=%.1f cached=%.1f ns/apply\n",
            counts[n], elapsed[0] / (100.0 * counts[n]), elapsed[1] / (100.0 * counts[n]));
    }
}

int main(void) {
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        assert(device);
        sg_setup(&(sg_desc){.environment.metal.device = device, .logger.func = logger});
        sg_enable_stats();
        const char* source =
            "#include <metal_stdlib>\nusing namespace metal;\n"
            "struct U { float4 values[150]; };\n"
            "vertex float4 vs(uint v [[vertex_id]]) { float2 p[3]={float2(-1,-1),float2(3,-1),float2(-1,3)}; return float4(p[v],0,1); }\n"
            "fragment float4 fs(constant U& u [[buffer(2)]]) {return u.values[149];}\n";
        sg_shader_desc desc = {
            .vertex_func = {.source = source, .entry = "vs"},
            .fragment_func = {.source = source, .entry = "fs"},
            .uniform_blocks[3] = {.stage = SG_SHADERSTAGE_FRAGMENT, .size = 2400, .msl_buffer_n = 2},
        };
        sg_shader shaders[2] = {sg_make_shader(&desc), sg_make_shader(&desc)};
        sg_pipeline pipelines[3];
        for (int i = 0; i < 3; ++i) {
            assert(sg_query_shader_state(shaders[i == 2]) == SG_RESOURCESTATE_VALID);
            pipelines[i] = sg_make_pipeline(&(sg_pipeline_desc){.shader = shaders[i == 2],
                .depth.pixel_format = SG_PIXELFORMAT_NONE, .colors[0].pixel_format = SG_PIXELFORMAT_RGBA8});
            assert(sg_query_pipeline_state(pipelines[i]) == SG_RESOURCESTATE_VALID);
        }
        sg_image image = sg_make_image(&(sg_image_desc){.width = 4, .height = 1, .pixel_format = SG_PIXELFORMAT_RGBA8, .usage.color_attachment = true});
        sg_view color = sg_make_view(&(sg_view_desc){.color_attachment.image = image});
        assert(sg_query_image_state(image) == SG_RESOURCESTATE_VALID && sg_query_view_state(color) == SG_RESOURCESTATE_VALID);
        if (getenv("SLOPA_UNIFORM_BENCH")) benchmark(color, pipelines[0]);
        sg_enable_stats();
        float red[600] = {0}, green[600] = {0};
        red[596] = red[599] = green[597] = green[599] = 1;
        uint64_t red_id = sg_alloc_uniform_data_id(), green_id = sg_alloc_uniform_data_id();
        assert(red_id && green_id > red_id);
        for (int frame = 0; frame < SG_NUM_INFLIGHT_FRAMES + 2; ++frame) {
            for (int pass = 0; pass < 2; ++pass) {
                sg_pass target = {.attachments.colors[0] = color};
                if (pass) target.action.colors[0].load_action = SG_LOADACTION_LOAD;
                if (pass) sg_reset_state_cache();
                sg_begin_pass(&target);
                for (int x = pass * 2; x < pass * 2 + 2; ++x) {
                    // Same-shader pipeline switch, then a new shader, then an ID change.
                    sg_apply_pipeline(pipelines[x == 2 ? 2 : x == 1 ? 1 : 0]);
                    sg_apply_scissor_rect(x, 0, 1, 1, true);
                    if (x == 3) sg_apply_uniforms_cached(3, &SG_RANGE(green), green_id);
                    else sg_apply_uniforms_cached(3, &SG_RANGE(red), red_id);
                    sg_draw(0, 3, 1);
                }
                sg_end_pass();
            }
            assert(_sg.mtl.cur_ub_offset == 3 * _sg_roundup(2400, _SG_MTL_UB_ALIGN));
            id<MTLCommandBuffer> command = [(__bridge id<MTLCommandBuffer>)sg_mtl_command_buffer() retain];
            id<MTLTexture> texture = (__bridge id<MTLTexture>)sg_mtl_query_image_info(image).tex[0];
            id<MTLBuffer> readback = [device newBufferWithLength:256 options:MTLResourceStorageModeShared];
            assert(readback);
            id<MTLBlitCommandEncoder> blit = [command blitCommandEncoder];
            assert(blit);
            [blit copyFromTexture:texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
                sourceSize:MTLSizeMake(4,1,1) toBuffer:readback destinationOffset:0 destinationBytesPerRow:256 destinationBytesPerImage:256];
            [blit endEncoding];
            sg_commit();
            [command waitUntilCompleted]; // Test-only readback.
            assert(command.status == MTLCommandBufferStatusCompleted);
            const uint8_t* pixels = readback.contents;
            for (int x = 0; x < 4; ++x) {
                assert(pixels[x*4] == (x == 3 ? 0 : 255));
                assert(pixels[x*4+1] == (x == 3 ? 255 : 0));
                assert(pixels[x*4+2] == 0 && pixels[x*4+3] == 255);
            }
            assert(sg_query_stats().prev_frame.num_reuse_uniforms == 1);
            assert(sg_query_stats().prev_frame.size_reuse_uniforms == 2400);
            [readback release]; [command release];
        }
        sg_shutdown();
        sg_setup(&(sg_desc){.environment.metal.device = device, .logger.func = logger});
        assert(sg_alloc_uniform_data_id() > green_id);
        sg_shutdown();
        [device release];
    }
    puts("Metal uniform IDs: exact pixels across pipelines, passes, IDs, frame rotation and setup");
}
