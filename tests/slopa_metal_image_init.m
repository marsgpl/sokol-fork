// Headless Metal textures: incremental initialization, pitches, mips and sealing.
#define SOKOL_IMPL
#define SOKOL_METAL
#include "sokol_gfx.h"
#include <stdio.h>

static void check_compressed(sg_pixel_format format) {
    if (!sg_query_pixelformat(format).sample) {
        printf("Metal compressed init: format %d unavailable\n", format);
        return;
    }
    sg_image_desc desc = {.width = 12, .height = 20, .num_mipmaps = 5, .pixel_format = format, .usage.write_unsealed = true};
    sg_image image = sg_make_image(&desc);
    assert(sg_query_image_state(image) == SG_RESOURCESTATE_UNSEALED);
    sg_mtl_image_info native = sg_mtl_query_image_info(image);
    assert(native.active_slot == 0 && native.tex[0] && !native.tex[1]);
    id<MTLTexture> texture = (__bridge id<MTLTexture>)native.tex[0];
    uint8_t data[512], actual[512], expected[512];
    for (size_t i = 0; i < sizeof data; ++i) data[i] = (uint8_t)(i * 17 + i / 7);
    for (int mip = 0; mip < 5; ++mip) {
        int w = _sg_miplevel_dim(12, mip), h = _sg_miplevel_dim(20, mip);
        int pitch = ((w + 3) / 4) * 16;
        size_t bytes = (size_t)pitch * ((h + 3) / 4);
        sg_write_image_unsealed(&(sg_write_image_desc){.src.data = {data, bytes}, .dst = {.image = image, .mip_level = mip}});
        [texture getBytes:actual bytesPerRow:(NSUInteger)pitch fromRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:(NSUInteger)mip];
        assert(memcmp(actual, data, bytes) == 0);
    }
    memcpy(expected, data, 240);
    sg_write_image_desc patch = {.src = {.data = {data, 96}, .offset = 16, .bytes_per_row = 48, .bytes_per_slice = 96},
        .dst = {.image = image, .x = 4, .y = 4}, .size = {.width = 8, .height = 8}};
    sg_write_image_unsealed(&patch);
    memcpy(expected + 64, data + 16, 32); memcpy(expected + 112, data + 64, 32);
    sg_write_image_desc edge = {.src.data = {data, 16}, .src.bytes_per_row = 16,
        .dst = {.image = image, .mip_level = 1, .x = 4, .y = 8}};
    sg_write_image_unsealed(&edge);
    [texture getBytes:actual bytesPerRow:32 fromRegion:MTLRegionMake2D(0, 0, 6, 10) mipmapLevel:1];
    assert(memcmp(actual, data, 80) == 0 && memcmp(actual + 80, data, 16) == 0);
    unsigned before = sg_query_stats().cur_frame.external_transfers.num_write_texture;
    sg_write_image_desc bad = patch; bad.dst.x = 1;
    sg_write_image_unsealed(&bad);
    bad = patch; bad.src.data.size = 95; sg_write_image_unsealed(&bad);
    assert(sg_query_stats().cur_frame.external_transfers.num_write_texture == before);
    sg_seal_image(image);
    assert(sg_query_image_state(image) == SG_RESOURCESTATE_VALID);
    patch.src.offset = 0; sg_write_image_unsealed(&patch);
    [texture getBytes:actual bytesPerRow:48 fromRegion:MTLRegionMake2D(0, 0, 12, 20) mipmapLevel:0];
    assert(memcmp(actual, expected, 240) == 0);
    sg_destroy_image(image);
    image = sg_make_image(&desc);
    patch.dst.image = image; sg_write_image_unsealed(&patch);
    sg_uninit_image(image); sg_init_image(image, &desc);
    assert(sg_query_image_state(image) == SG_RESOURCESTATE_UNSEALED);
    sg_destroy_image(image);
    printf("Metal compressed init: format %d mip/subrect readback passed\n", format);
}

int main(void) {
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        assert(device);
        sg_setup(&(sg_desc){ .environment.metal.device = (__bridge const void*)device});
        sg_enable_stats();
        sg_image_desc desc = {.width = 13, .height = 7, .num_mipmaps = 4, .usage.write_unsealed = true};
        sg_image image = sg_make_image(&desc);
        assert(sg_query_image_state(image) == SG_RESOURCESTATE_UNSEALED);
        _sg.desc.disable_validation = true; // Exercise the production view-state gate too.
        sg_view view = sg_make_view(&(sg_view_desc){.texture.image = image});
        assert(sg_query_view_state(view) == SG_RESOURCESTATE_FAILED);
        sg_destroy_view(view);
        sg_mtl_image_info native = sg_mtl_query_image_info(image);
        assert(native.active_slot == 0 && native.tex[0] && !native.tex[1]);
        id<MTLTexture> texture = (__bridge id<MTLTexture>)native.tex[0];
        uint8_t data[512], actual[512], expected[512];
        for (size_t i = 0; i < sizeof data; ++i) data[i] = (uint8_t)(i * 17 + i / 7);
        for (int mip = 0; mip < 4; ++mip) {
            int w = _sg_miplevel_dim(13, mip), h = _sg_miplevel_dim(7, mip);
            size_t bytes = (size_t)w * h * 4;
            sg_write_image_unsealed(&(sg_write_image_desc){.src.data = {data, bytes}, .dst = {.image = image, .mip_level = mip}});
            [texture getBytes:actual bytesPerRow:(NSUInteger)(w * 4) fromRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:(NSUInteger)mip];
            assert(memcmp(actual, data, bytes) == 0);
        }
        memcpy(expected, data, 13 * 7 * 4);
        sg_write_image_desc patch = {.src = {.data = {data, sizeof data}, .offset = 4, .bytes_per_row = 32, .bytes_per_slice = 96},
            .dst = {.image = image, .x = 3, .y = 2}, .size = {.width = 5, .height = 3}};
        sg_write_image_unsealed(&patch);
        for (int y = 0; y < 3; ++y) memcpy(expected + ((y + 2) * 13 + 3) * 4, data + 4 + y * 32, 20);
        sg_write_image_desc bad = patch;
        bad.dst.mip_level = 1000;
        sg_write_image_unsealed(&bad);
        bad = patch; bad.src.data.size = 87; // final source byte is at offset 87
        sg_write_image_unsealed(&bad);
        sg_seal_image(image);
        assert(sg_query_image_state(image) == SG_RESOURCESTATE_VALID);
        // A valid post-seal write would change the texture; the API must reject it.
        patch.src.offset = 8;
        sg_write_image_unsealed(&patch);
        [texture getBytes:actual bytesPerRow:52 fromRegion:MTLRegionMake2D(0, 0, 13, 7) mipmapLevel:0];
        assert(memcmp(actual, expected, 13 * 7 * 4) == 0);
        view = sg_make_view(&(sg_view_desc){.texture.image = image});
        assert(sg_query_view_state(view) == SG_RESOURCESTATE_VALID);
        sg_frame_stats_transfers transfers = sg_query_stats().cur_frame.external_transfers;
        assert(transfers.num_write_texture == 5 && transfers.size_write_texture == 512);
        sg_destroy_view(view);
        sg_destroy_image(image);
        for (int i = 0; i < 3; ++i) {
            image = sg_make_image(&desc);
            assert(sg_query_image_state(image) == SG_RESOURCESTATE_UNSEALED);
            sg_uninit_image(image);
            assert(sg_query_image_state(image) == SG_RESOURCESTATE_ALLOC);
            sg_init_image(image, &desc);
            assert(sg_query_image_state(image) == SG_RESOURCESTATE_UNSEALED);
            sg_destroy_image(image);
        }
        check_compressed(SG_PIXELFORMAT_BC7_RGBA);
        check_compressed(SG_PIXELFORMAT_ASTC_4x4_RGBA);
        check_compressed(SG_PIXELFORMAT_ETC2_RGBA8);
        image = sg_make_image(&desc); // shutdown must also collect unsealed textures
        sg_shutdown();
        [device release];
    }
    puts("Sokol Metal P2: real texture mip/subrect bytes, padded sources, sealing and cleanup passed");
}
