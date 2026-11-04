// LLM maintained. Public resource APIs on actual offscreen Metal and browser WebGPU.
#define SOKOL_IMPL
#include "sokol_gfx.h"
#include <stdio.h>
#if defined(SOKOL_WGPU)
#include <emscripten/emscripten.h>
extern WGPUDevice emscripten_webgpu_get_device(void);
#define EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define EXPORT
#endif
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "failed: %s:%d\n", #c, __LINE__); abort(); } } while (0)

static sg_buffer result;
static sg_image images[7];
static uint8_t expected_buffer[64], rgba[84], patched[64], blocks[56], layers[32];

static void fail_log(const char* tag, uint32_t level, uint32_t item, const char* msg,
    uint32_t line, const char* file, void* data) {
    (void)tag; (void)item; (void)file; (void)data;
    if (level <= 1) { fprintf(stderr, "%u: %s\n", line, msg ? msg : "error"); abort(); }
}

EXPORT void probe_init(void) {
    sg_desc desc = {
        .environment.defaults = {.color_format = SG_PIXELFORMAT_RGBA8, .depth_format = SG_PIXELFORMAT_NONE, .sample_count = 1},
        .logger.func = fail_log,
    };
    #if defined(SOKOL_METAL)
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    CHECK(device);
    desc.environment.metal.device = (__bridge const void*)device;
    #else
    desc.environment.wgpu.device = emscripten_webgpu_get_device();
    #endif
    sg_setup(&desc);
    #if defined(SOKOL_METAL)
    _SG_OBJC_RELEASE(device);
    #endif
    CHECK(sg_isvalid());
}

EXPORT void probe_prepare(void) {
    uint8_t initial[32], upload[32], patch[8];
    for (int i = 0; i < 32; i++) { initial[i] = (uint8_t)(i + 1); upload[i] = (uint8_t)(128 + i); }
    for (int i = 0; i < 8; i++) { patch[i] = (uint8_t)(201 + i); }
    memcpy(expected_buffer + 16, initial, 32);
    memcpy(expected_buffer + 20, patch, 8);
    memcpy(expected_buffer + 28, upload + 4, 8);
    sg_buffer persistent = sg_make_buffer(&(sg_buffer_desc){
        .data = SG_RANGE(initial), .usage = {.copy_src = true, .copy_dst = true}, .label = "sulrim-retired-buffer",
    });
    sg_buffer source = sg_make_buffer(&(sg_buffer_desc){.size = 32, .usage = {.write_unsealed = true, .copy_src = true}});
    CHECK(sg_query_buffer_state(source) == SG_RESOURCESTATE_UNSEALED);
    sg_write_buffer_unsealed(&(sg_write_buffer_desc){.src.data = SG_RANGE(upload), .dst.buffer = source, .size = 16});
    sg_commit();
    sg_write_buffer_unsealed(&(sg_write_buffer_desc){
        .src = {.data = SG_RANGE(upload), .offset = 16}, .dst = {.buffer = source, .offset = 16}, .size = 16,
    });
    sg_seal_buffer(source);
    sg_copy_buffer_to_buffer(&(sg_copy_buffer_to_buffer_desc){
        .src = {.buffer = source, .offset = 4}, .dst = {.buffer = persistent, .offset = 12}, .size = 8,
    });
    sg_destroy_buffer(source); sg_commit();
    sg_buffer staging = sg_make_buffer(&(sg_buffer_desc){
        .size = 512, .usage = {.staging_buffer = true, .write_transient = true, .copy_src = true},
    });
    sg_write_buffer_transient(&(sg_write_buffer_desc){.src.data = SG_RANGE(patch), .dst.buffer = staging});
    sg_copy_buffer_to_buffer(&(sg_copy_buffer_to_buffer_desc){
        .src.buffer = staging, .dst = {.buffer = persistent, .offset = 4}, .size = 8,
    });
    sg_commit();
    uint8_t zeros[64] = {0};
    result = sg_make_buffer(&(sg_buffer_desc){.data = SG_RANGE(zeros), .usage = {.copy_src = true, .copy_dst = true}});
    sg_copy_buffer_to_buffer(&(sg_copy_buffer_to_buffer_desc){
        .src.buffer = persistent, .dst = {.buffer = result, .offset = 16}, .size = 32,
    });
    sg_destroy_buffer(persistent); sg_commit();

    for (int i = 0; i < 84; i++) { rgba[i] = (uint8_t)(17 + 3 * i); }
    sg_image_desc desc = {.width = 4, .height = 4, .num_mipmaps = 3, .pixel_format = SG_PIXELFORMAT_RGBA8, .usage.copy_src = true};
    desc.data = SG_RANGE(rgba);
    images[0] = sg_make_image(&desc);
    desc.data = (sg_range){0}; desc.usage.write_unsealed = true;
    images[1] = sg_make_image(&desc);
    CHECK(sg_query_image_state(images[1]) == SG_RESOURCESTATE_UNSEALED);
    size_t offset = 0;
    for (int mip = 0; mip < 3; mip++) {
        int side = 4 >> mip, row = side * 4 + 16;
        for (int y = 0; y < side; y++) {
            uint8_t padded[64]; memset(padded, 0xEE, sizeof padded);
            memcpy(padded + 8, rgba + offset + y * side * 4, (size_t)side * 4);
            sg_write_image_unsealed(&(sg_write_image_desc){
                .src = {.data = SG_RANGE(padded), .offset = 8, .bytes_per_row = row, .bytes_per_slice = row},
                .dst = {.image = images[1], .mip_level = mip, .y = y}, .size = {.width = side, .height = 1},
            });
            sg_commit(); // initialization may span frames; the image remains unsealed
            CHECK(sg_query_image_state(images[1]) == SG_RESOURCESTATE_UNSEALED);
        }
        offset += (size_t)side * side * 4;
    }
    sg_seal_image(images[1]);
    desc.num_mipmaps = 1; desc.usage = (sg_image_usage){.copy_src = true, .copy_dst = true};
    desc.data = (sg_range){0};
    images[2] = sg_make_image(&desc);
    desc.data = (sg_range){0}; desc.label = "sulrim-retired-image";
    sg_image temporary = sg_make_image(&desc);
    sg_copy_image_to_image(&(sg_copy_image_to_image_desc){.src.image = images[1], .dst.image = temporary});
    sg_copy_image_to_image(&(sg_copy_image_to_image_desc){.src.image = temporary, .dst.image = images[2]});
    sg_destroy_image(temporary); sg_commit();
    memcpy(patched, rgba, 64);
    uint8_t rows[512]; memset(rows, 0xEE, sizeof rows);
    for (int y = 0; y < 2; y++) {
        for (int x = 0; x < 8; x++) { rows[y * 256 + x] = (uint8_t)(51 + y * 8 + x); }
        memcpy(patched + (y + 1) * 16 + 4, rows + y * 256, 8);
    }
    sg_write_buffer_transient(&(sg_write_buffer_desc){.src.data = SG_RANGE(rows), .dst.buffer = staging});
    sg_copy_buffer_to_image(&(sg_copy_buffer_to_image_desc){
        .src = {.buffer = staging, .bytes_per_row = 256, .bytes_per_slice = 512}, .dst = {.image = images[2], .x = 1, .y = 1},
        .size = {.width = 2, .height = 2},
    });
    sg_destroy_buffer(staging); sg_commit();
    if (sg_query_pixelformat(SG_PIXELFORMAT_BC1_RGBA).sample) {
        for (int i = 0; i < 56; i++) { blocks[i] = (uint8_t)(7 + 5 * i); }
        desc = (sg_image_desc){.width = 8, .height = 8, .num_mipmaps = 4, .pixel_format = SG_PIXELFORMAT_BC1_RGBA,
            .usage.copy_src = true, .data = SG_RANGE(blocks)};
        images[3] = sg_make_image(&desc);
        desc.data = (sg_range){0}; desc.usage.write_unsealed = true;
        images[4] = sg_make_image(&desc);
        offset = 0;
        for (int mip = 0; mip < 4; mip++) {
            int side = 8 >> mip, bytes = mip == 0 ? 32 : 8;
            sg_write_image_unsealed(&(sg_write_image_desc){
                .src = {.data = SG_RANGE(blocks), .offset = offset}, .dst = {.image = images[4], .mip_level = mip},
                .size = {.width = side, .height = side},
            });
            offset += bytes; sg_commit();
        }
        sg_seal_image(images[4]);
    }
    for (int i = 0; i < 32; i++) { layers[i] = (uint8_t)(231 - i * 3); }
    desc = (sg_image_desc){.type = SG_IMAGETYPE_ARRAY, .width = 2, .height = 2, .num_slices = 2,
        .pixel_format = SG_PIXELFORMAT_RGBA8, .usage.copy_src = true, .data = SG_RANGE(layers)};
    images[5] = sg_make_image(&desc);
    desc.type = SG_IMAGETYPE_3D; desc.data = (sg_range){0}; desc.usage.write_unsealed = true;
    images[6] = sg_make_image(&desc);
    uint8_t volume[64]; memset(volume, 0xEE, sizeof volume);
    for (int slice = 0; slice < 2; slice++) {
        for (int y = 0; y < 2; y++) { memcpy(volume + 8 + slice * 36 + y * 12, layers + slice * 16 + y * 8, 8); }
    }
    sg_write_image_unsealed(&(sg_write_image_desc){
        .src = {.data = SG_RANGE(volume), .offset = 8, .bytes_per_row = 12, .bytes_per_slice = 36},
        .dst.image = images[6], .size = {.width = 2, .height = 2, .num_slices = 2},
    });
    sg_seal_image(images[6]);
    for (int i = 0; i < 7; i++) { CHECK(!images[i].id || sg_query_image_state(images[i]) == SG_RESOURCESTATE_VALID); }
    sg_commit();
}

EXPORT const void* probe_expected_buffer(void) { return expected_buffer; }
EXPORT const void* probe_expected_image(int index, int mip, int slice) {
    if (index >= 5) { return layers + slice * 16; }
    size_t offset = index >= 3 ? (mip ? 32 + (mip - 1) * 8 : 0) : (mip == 0 ? 0 : mip == 1 ? 64 : 80);
    return index >= 3 ? blocks + offset : index == 2 ? patched : rgba + offset;
}
EXPORT const void* probe_buffer(void) {
    #if defined(SOKOL_WGPU)
    return sg_wgpu_query_buffer_info(result).buf;
    #else
    sg_mtl_buffer_info info = sg_mtl_query_buffer_info(result); return info.buf[info.active_slot];
    #endif
}
EXPORT const void* probe_image(int index) {
    if (!images[index].id) { return 0; }
    #if defined(SOKOL_WGPU)
    return sg_wgpu_query_image_info(images[index]).tex;
    #else
    sg_mtl_image_info info = sg_mtl_query_image_info(images[index]); return info.tex[info.active_slot];
    #endif
}
EXPORT void probe_shutdown(void) { sg_shutdown(); }

#if defined(SOKOL_METAL)
int main(void) {
    @autoreleasepool {
        probe_init(); probe_prepare();
        id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)sg_mtl_command_queue();
        id<MTLCommandBuffer> commands = [queue commandBuffer];
        id<MTLBlitCommandEncoder> blit = [commands blitCommandEncoder];
        CHECK(commands && blit);
        id<MTLBuffer> reads[7][4] = {{nil}};
        for (int index = 0; index < 7; index++) {
            if (!probe_image(index)) { continue; }
            id<MTLTexture> tex = (__bridge id<MTLTexture>)probe_image(index);
            int levels = index >= 5 ? 1 : index >= 3 ? 4 : index == 2 ? 1 : 3;
            for (int mip = 0; mip < levels; mip++) {
                int side = index >= 5 ? 2 : (index >= 3 ? 8 : 4) >> mip;
                reads[index][mip] = [_sg.mtl.device newBufferWithLength:256 * 8 options:MTLResourceStorageModeShared];
                CHECK(reads[index][mip]);
                for (int slice = 0; slice < (index >= 5 ? 2 : 1); slice++) {
                    [blit copyFromTexture:tex sourceSlice:index == 5 ? slice : 0 sourceLevel:mip
                        sourceOrigin:MTLOriginMake(0, 0, index == 6 ? slice : 0)
                        sourceSize:MTLSizeMake(side, side, 1) toBuffer:reads[index][mip] destinationOffset:slice * 512
                        destinationBytesPerRow:256 destinationBytesPerImage:256 * (index == 3 || index == 4 ? (side + 3) / 4 : side)];
                }
            }
        }
        [blit endEncoding]; [commands commit]; [commands waitUntilCompleted];
        CHECK(commands.status == MTLCommandBufferStatusCompleted);
        id<MTLBuffer> native = (__bridge id<MTLBuffer>)probe_buffer();
        CHECK(memcmp([native contents], expected_buffer, 64) == 0);
        int checks = 1;
        for (int index = 0; index < 7; index++) {
            int levels = index >= 5 ? 1 : index >= 3 ? 4 : index == 2 ? 1 : 3;
            for (int mip = 0; mip < levels && reads[index][mip]; mip++) {
                int side = index >= 5 ? 2 : (index >= 3 ? 8 : 4) >> mip;
                int row = index == 3 || index == 4 ? ((side + 3) / 4) * 8 : side * 4;
                int rows = index == 3 || index == 4 ? (side + 3) / 4 : side;
                for (int slice = 0; slice < (index >= 5 ? 2 : 1); slice++) {
                    const uint8_t* expected = probe_expected_image(index, mip, slice);
                    for (int y = 0; y < rows; y++) { CHECK(memcmp((uint8_t*)[reads[index][mip] contents] + slice * 512 + y * 256, expected + y * row, row) == 0); }
                    checks++;
                }
                _SG_OBJC_RELEASE(reads[index][mip]);
            }
        }
        printf("Metal resources: %d GPU byte readbacks; BC1=%d; persistent growth, offset/row/mip writes and retirement: PASS\n", checks, images[3].id != 0);
        probe_shutdown();
    }
}
#endif
