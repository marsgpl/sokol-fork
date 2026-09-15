// Headless Metal: base instance must reach a storage lookup for one instance.
#define SOKOL_IMPL
#define SOKOL_METAL
#include "sokol_gfx.h"
#include <stdio.h>

static void logger(const char* tag, uint32_t level, uint32_t item, const char* message, uint32_t line, const char* file, void* user) {
    (void)tag; (void)level; (void)item; (void)line; (void)file; (void)user;
    fprintf(stderr, "%s\n", message ? message : "Sokol error");
}

int main(void) {
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        assert(device);
        sg_setup(&(sg_desc){.environment.metal.device = device, .logger.func = logger});
        sg_enable_stats();
        const char* source =
            "#include <metal_stdlib>\nusing namespace metal;\n"
            "struct Out { float4 pos [[position]]; float value; };\n"
            "vertex Out vs(uint v [[vertex_id]], uint i [[instance_id]], device const uint* values [[buffer(8)]]) {\n"
            "float2 p[3] = {float2(-1,-1),float2(3,-1),float2(-1,3)};\n"
            "return {float4(p[v],0,1),float(values[i])/255.0}; }\n"
            "fragment float4 fs(Out in [[stage_in]]) { return float4(in.value,0,0,1); }\n";
        sg_shader shader = sg_make_shader(&(sg_shader_desc){
            .vertex_func = {.source = source, .entry = "vs"},
            .fragment_func = {.source = source, .entry = "fs"},
            .views[0].storage_buffer = {.stage = SG_SHADERSTAGE_VERTEX, .readonly = true, .msl_buffer_n = 8},
        });
        assert(sg_query_shader_state(shader) == SG_RESOURCESTATE_VALID);
        uint32_t values[16];
        for (int i = 0; i < 16; ++i) values[i] = (uint32_t)(13 * i + 5);
        sg_buffer storage = sg_make_buffer(&(sg_buffer_desc){.usage.storage_buffer = true, .data = SG_RANGE(values)});
        sg_view storage_view = sg_make_view(&(sg_view_desc){.storage_buffer.buffer = storage});
        uint16_t indices[] = {0, 1, 2};
        sg_buffer index_buffer = sg_make_buffer(&(sg_buffer_desc){.usage.index_buffer = true, .data = SG_RANGE(indices)});
        sg_image image = sg_make_image(&(sg_image_desc){.width = 1, .height = 1, .pixel_format = SG_PIXELFORMAT_RGBA8, .usage.color_attachment = true});
        sg_view color = sg_make_view(&(sg_view_desc){.color_attachment.image = image});
        assert(sg_query_buffer_state(storage) == SG_RESOURCESTATE_VALID);
        assert(sg_query_buffer_state(index_buffer) == SG_RESOURCESTATE_VALID);
        assert(sg_query_image_state(image) == SG_RESOURCESTATE_VALID);
        assert(sg_query_view_state(storage_view) == SG_RESOURCESTATE_VALID && sg_query_view_state(color) == SG_RESOURCESTATE_VALID);
        for (int indexed = 0; indexed < 2; ++indexed) {
            sg_pipeline pipeline = sg_make_pipeline(&(sg_pipeline_desc){.shader = shader,
                .index_type = indexed ? SG_INDEXTYPE_UINT16 : SG_INDEXTYPE_NONE,
                .depth.pixel_format = SG_PIXELFORMAT_NONE, .colors[0].pixel_format = SG_PIXELFORMAT_RGBA8});
            assert(sg_query_pipeline_state(pipeline) == SG_RESOURCESTATE_VALID);
            for (int base = 0; base <= 7; base += 7) {
                sg_begin_pass(&(sg_pass){.attachments.colors[0] = color});
                sg_apply_pipeline(pipeline);
                sg_apply_bindings(&(sg_bindings){.views[0] = storage_view, .index_buffer = indexed ? index_buffer : (sg_buffer){0}});
                assert(!_sg.use_instanced_draw);
                sg_draw_ex(0, 3, 1, 0, base);
                sg_end_pass();
                id<MTLCommandBuffer> command = [(__bridge id<MTLCommandBuffer>)sg_mtl_command_buffer() retain];
                id<MTLTexture> texture = (__bridge id<MTLTexture>)sg_mtl_query_image_info(image).tex[0];
                id<MTLBuffer> readback = [device newBufferWithLength:256 options:MTLResourceStorageModeShared];
                assert(readback);
                id<MTLBlitCommandEncoder> blit = [command blitCommandEncoder];
                assert(blit);
                [blit copyFromTexture:texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
                    sourceSize:MTLSizeMake(1, 1, 1) toBuffer:readback destinationOffset:0 destinationBytesPerRow:256 destinationBytesPerImage:256];
                [blit endEncoding];
                sg_commit();
                [command waitUntilCompleted]; // Test-only readback, never a game frame.
                assert(command.status == MTLCommandBufferStatusCompleted);
                [command release];
                const uint8_t* pixel = readback.contents;
                assert(pixel[0] == values[base] && pixel[1] == 0 && pixel[2] == 0 && pixel[3] == 255);
                [readback release];
                assert(sg_query_stats().prev_frame.num_draw_ex == 1 && sg_query_stats().prev_frame.num_draw == 0);
            }
            sg_destroy_pipeline(pipeline);
        }
        sg_destroy_view(color); sg_destroy_image(image);
        sg_destroy_view(storage_view); sg_destroy_buffer(storage); sg_destroy_buffer(index_buffer);
        sg_destroy_shader(shader);
        sg_shutdown();
        [device release];
    }
    puts("Metal direct offsets: indexed/non-indexed singleton storage lookups read back exactly");
}
