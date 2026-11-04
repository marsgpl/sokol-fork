// LLM maintained. Texture eligibility, separate submission, actual pixels and shutdown completion.
#define SOKOL_IMPL
#define SOKOL_METAL
#include "sokol_gfx.h"
#include <objc/runtime.h>
#include <stdio.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "failed: %s:%d\n", #c, __LINE__); abort(); } } while (0)

static IMP original_optimize, original_commit, original_wait;
static unsigned optimize_calls, frame_commits, opt_commits, opt_waits, pixels;
static id<MTLSharedEvent> wait_gate;
static const uint8_t texels[16] = {23,99,173,255, 23,99,173,255, 23,99,173,255, 23,99,173,255};

static void observe_optimize(id encoder, SEL selector, id<MTLTexture> texture) {
    CHECK(encoder == _sg.mtl.opt_blit_encoder && _sg.mtl.opt_cmd_buffer.retainedReferences);
    CHECK(_sg.mtl.opt_cmd_buffer.status == MTLCommandBufferStatusNotEnqueued);
    CHECK(texture.storageMode == MTLStorageModeShared);
    optimize_calls++;
    ((void (*)(id, SEL, id))original_optimize)(encoder, selector, texture);
}
static void observe_commit(id<MTLCommandBuffer> commands, SEL selector) {
    if (commands == _sg.mtl.opt_cmd_buffer) {
        CHECK(!_sg.mtl.cmd_buffer || _sg.mtl.cmd_buffer.status >= MTLCommandBufferStatusCommitted);
        opt_commits++;
        if (wait_gate) [commands encodeWaitForEvent:wait_gate value:1];
    } else if (commands == _sg.mtl.cmd_buffer) {
        frame_commits++;
    }
    ((void (*)(id, SEL))original_commit)(commands, selector);
}
static void observe_wait(id<MTLCommandBuffer> commands, SEL selector) {
    if (commands == _sg.mtl.opt_last_submitted) {
        CHECK(_sg.mtl.valid);
        opt_waits++;
        if (wait_gate) {
            CHECK([commands status] < MTLCommandBufferStatusCompleted);
            wait_gate.signaledValue = 1;
        }
    }
    ((void (*)(id, SEL))original_wait)(commands, selector);
    CHECK([commands status] == MTLCommandBufferStatusCompleted);
}
static void fail_log(const char* tag, uint32_t level, uint32_t item, const char* msg,
    uint32_t line, const char* file, void* data) {
    (void)tag; (void)item; (void)file; (void)data;
    if (level <= 1) { fprintf(stderr, "%u: %s\n", line, msg ? msg : "error"); abort(); }
}
static sg_image make_image(sg_image_desc desc) {
    sg_image image = sg_make_image(&desc);
    CHECK(sg_query_image_state(image) == (desc.usage.write_unsealed ? SG_RESOURCESTATE_UNSEALED : SG_RESOURCESTATE_VALID));
    return image;
}
static sg_image_desc initial_desc(void) {
    return (sg_image_desc){.width = 2, .height = 2, .data = SG_RANGE(texels)};
}
static void setup(id<MTLDevice> device, bool managed, bool retained) {
    sg_setup(&(sg_desc){.environment = {.metal.device = (__bridge const void*)device,
        .defaults = {.color_format = SG_PIXELFORMAT_RGBA8, .depth_format = SG_PIXELFORMAT_NONE, .sample_count = 1}},
        .metal = {.force_managed_storage_mode = managed, .use_command_buffer_with_retained_references = retained},
        .logger.func = fail_log});
    CHECK(sg_isvalid());
}
static void eligibility(bool shared) {
    unsigned start = optimize_calls;
    sg_image_desc desc = initial_desc();
    sg_destroy_image(make_image(desc));
    CHECK(optimize_calls == start + shared);
    desc.usage.copy_src = true;
    sg_destroy_image(make_image(desc));
    CHECK(optimize_calls == start + 2*shared);
    desc.usage.copy_dst = true;
    sg_destroy_image(make_image(desc));
    desc.usage = (sg_image_usage){.storage_image = true};
    sg_destroy_image(make_image(desc));
    desc.data = (sg_range){0};
    const sg_image_usage roles[] = {
        {.write_unsealed = true}, {.write_transient = true}, {.copy_dst = true},
        {.storage_image = true}, {.color_attachment = true}, {.resolve_attachment = true},
    };
    for (size_t i = 0; i < sizeof roles / sizeof roles[0]; i++) {
        desc.usage = roles[i];
        sg_image image = make_image(desc);
        if (roles[i].write_unsealed) {
            sg_write_image_unsealed(&(sg_write_image_desc){.src.data = SG_RANGE(texels), .dst.image = image});
            sg_seal_image(image);
        } else if (roles[i].write_transient) {
            sg_write_image_transient(&(sg_write_image_desc){.src.data = SG_RANGE(texels), .dst.image = image});
        }
        sg_destroy_image(image);
    }
    desc.usage = (sg_image_usage){.depth_stencil_attachment = true}; desc.pixel_format = SG_PIXELFORMAT_DEPTH;
    sg_destroy_image(make_image(desc));
    uint8_t compressed[8] = {0};
    desc = (sg_image_desc){.width = 4, .height = 4, .pixel_format = SG_PIXELFORMAT_BC1_RGBA, .data = SG_RANGE(compressed)};
    if (sg_query_pixelformat(desc.pixel_format).sample) sg_destroy_image(make_image(desc));
    MTLTextureDescriptor* native_desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:2 height:2 mipmapped:NO];
    native_desc.storageMode = shared ? MTLStorageModeShared : MTLStorageModeManaged;
    id<MTLTexture> native = [_sg.mtl.device newTextureWithDescriptor:native_desc]; CHECK(native);
    desc = initial_desc(); desc.data = (sg_range){0}; desc.mtl_texture = (__bridge const void*)native;
    sg_destroy_image(make_image(desc)); _SG_OBJC_RELEASE(native);
    CHECK(optimize_calls == start + 2*shared);
    sg_commit();
    CHECK(!_sg.mtl.opt_cmd_buffer && !_sg.mtl.opt_blit_encoder);
}
static void sampling(bool shared) {
    const char* source = "#include <metal_stdlib>\nusing namespace metal;\n"
        "vertex float4 vs(uint i [[vertex_id]]) {return float4(i==1?3.0:-1.0,i==2?3.0:-1.0,0,1);}\n"
        "fragment float4 fs(texture2d<float> t [[texture(0)]], sampler s [[sampler(0)]]) {return t.sample(s,float2(0.25));}\n"
        "kernel void cs() {}";
    sg_shader shader = sg_make_shader(&(sg_shader_desc){.vertex_func = {.source = source, .entry = "vs"},
        .fragment_func = {.source = source, .entry = "fs"}, .views[0].texture = {.stage = SG_SHADERSTAGE_FRAGMENT, .msl_texture_n = 0},
        .samplers[0] = {.stage = SG_SHADERSTAGE_FRAGMENT, .msl_sampler_n = 0},
        .texture_sampler_pairs[0] = {.stage = SG_SHADERSTAGE_FRAGMENT, .view_slot = 0, .sampler_slot = 0}});
    sg_pipeline render = sg_make_pipeline(&(sg_pipeline_desc){.shader = shader});
    sg_shader cs = sg_make_shader(&(sg_shader_desc){.compute_func = {.source = source, .entry = "cs"}, .mtl_threads_per_threadgroup = {32,1,1}});
    sg_pipeline compute = sg_make_pipeline(&(sg_pipeline_desc){.compute = true, .shader = cs});
    CHECK(sg_query_pipeline_state(render) == SG_RESOURCESTATE_VALID && sg_query_pipeline_state(compute) == SG_RESOURCESTATE_VALID);
    sg_sampler sampler = sg_make_sampler(&(sg_sampler_desc){0});
    sg_image color = make_image((sg_image_desc){.width = 4, .height = 4, .usage = {.color_attachment = true, .copy_src = true}});
    sg_view cv = sg_make_view(&(sg_view_desc){.color_attachment.image = color});
    sg_image image = {0}; sg_view view = {0};
    for (int frame = 0; frame < 2; frame++) {
        uint32_t data = 7;
        sg_buffer src = sg_make_buffer(&(sg_buffer_desc){.data = SG_RANGE(data), .usage.copy_src = true});
        sg_buffer dst = sg_make_buffer(&(sg_buffer_desc){.size = 4, .usage.copy_dst = true});
        sg_copy_buffer_to_buffer(&(sg_copy_buffer_to_buffer_desc){.src.buffer = src, .dst.buffer = dst, .size = 4});
        CHECK(_sg.mtl.blit_cmd_encoder);
        unsigned before = optimize_calls, before_commit = opt_commits, before_frame = frame_commits;
        sg_begin_pass(&(sg_pass){.attachments.colors[0] = cv});
        id<MTLRenderCommandEncoder> encoder = _sg.mtl.render_cmd_encoder; CHECK(encoder);
        if (!frame) {
            image = make_image(initial_desc()); view = sg_make_view(&(sg_view_desc){.texture.image = image});
            CHECK(optimize_calls == before + shared);
        }
        CHECK(_sg.mtl.render_cmd_encoder == encoder && !_sg.mtl.blit_cmd_encoder);
        sg_apply_pipeline(render); sg_apply_bindings(&(sg_bindings){.views[0] = view, .samplers[0] = sampler});
        sg_draw(0,3,1); sg_end_pass();
        sg_begin_pass(&(sg_pass){.compute = true});
        id<MTLComputeCommandEncoder> ce = _sg.mtl.compute_cmd_encoder;
        sg_image temporary = make_image(initial_desc()); sg_destroy_image(temporary);
        CHECK(_sg.mtl.compute_cmd_encoder == ce);
        sg_apply_pipeline(compute); sg_dispatch(1,1,1); sg_end_pass();
        id<MTLCommandBuffer> commands = (__bridge id<MTLCommandBuffer>)sg_mtl_command_buffer();
        id<MTLBuffer> read = [_sg.mtl.device newBufferWithLength:1024 options:MTLResourceStorageModeShared]; CHECK(read);
        id<MTLBlitCommandEncoder> blit = [commands blitCommandEncoder]; CHECK(blit);
        id<MTLTexture> texture = (__bridge id<MTLTexture>)sg_mtl_query_image_info(color).tex[0];
        [blit copyFromTexture:texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(4,4,1)
            toBuffer:read destinationOffset:0 destinationBytesPerRow:256 destinationBytesPerImage:1024];
        [blit endEncoding];
        sg_destroy_buffer(src); sg_destroy_buffer(dst);
        sg_commit();
        CHECK(frame_commits == before_frame + 1 && opt_commits == before_commit + shared);
        CHECK(!_sg.mtl.opt_cmd_buffer && !_sg.mtl.opt_blit_encoder);
        [commands waitUntilCompleted];
        const uint8_t* bytes = read.contents;
        for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) {CHECK(memcmp(bytes + y*256 + x*4, texels, 4) == 0); pixels++;}
        _SG_OBJC_RELEASE(read);
    }
    sg_destroy_view(view); sg_destroy_image(image); sg_destroy_view(cv); sg_destroy_image(color);
    sg_destroy_sampler(sampler); sg_destroy_pipeline(render); sg_destroy_pipeline(compute); sg_destroy_shader(shader); sg_destroy_shader(cs);
}
static void shutdown_case(id<MTLDevice> device, int submitted) {
    setup(device, false, false);
    CHECK(_sg.mtl.use_shared_storage_mode);
    wait_gate = [device newSharedEvent]; CHECK(wait_gate);
    unsigned before = opt_waits;
    sg_destroy_image(make_image(initial_desc()));
    if (submitted) sg_commit();
    if (submitted == 2) {
        sg_commit(); // An empty commit must preserve the last submitted batch.
        sg_destroy_image(make_image(initial_desc())); sg_commit();
    }
    sg_shutdown();
    CHECK(opt_waits == before + 1 && wait_gate.signaledValue == 1);
    CHECK(!_sg.mtl.opt_last_submitted && !_sg.mtl.opt_cmd_buffer && !_sg.mtl.opt_blit_encoder);
    _SG_OBJC_RELEASE(wait_gate);
}
int main(void) {
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice(); CHECK(device);
        CHECK([device supportsFamily:MTLGPUFamilyApple1]);
        id<MTLCommandQueue> queue = [device newCommandQueue]; CHECK(queue);
        id<MTLCommandBuffer> commands = [queue commandBuffer]; CHECK(commands);
        id<MTLBlitCommandEncoder> encoder = [commands blitCommandEncoder]; CHECK(encoder);
        Method optimize = class_getInstanceMethod(object_getClass(encoder), @selector(optimizeContentsForGPUAccess:));
        Method commit = class_getInstanceMethod(object_getClass(commands), @selector(commit));
        Method wait = class_getInstanceMethod(object_getClass(commands), @selector(waitUntilCompleted));
        CHECK(optimize && commit && wait);
        CHECK(commit == class_getInstanceMethod(object_getClass([queue commandBufferWithUnretainedReferences]), @selector(commit)));
        original_optimize = method_setImplementation(optimize, (IMP)observe_optimize);
        original_commit = method_setImplementation(commit, (IMP)observe_commit);
        original_wait = method_setImplementation(wait, (IMP)observe_wait);
        [encoder endEncoding]; [commands commit]; [commands waitUntilCompleted];
        for (int managed = 0; managed < 2; managed++) for (int retained = 0; retained < 2; retained++) {
            setup(device, managed != 0, retained != 0);
            eligibility(!managed); sampling(!managed); sg_shutdown();
        }
        for (int submitted = 0; submitted < 3; submitted++) shutdown_case(device, submitted);
        CHECK(pixels == 128 && optimize_calls > 0 && opt_commits > 0 && opt_waits == 5);
        method_setImplementation(optimize, original_optimize); method_setImplementation(commit, original_commit); method_setImplementation(wait, original_wait);
        _SG_OBJC_RELEASE(queue); _SG_OBJC_RELEASE(device);
        printf("Metal textures: %u pixels; %u optimize calls, %u batches, %u shutdown waits: PASS\n", pixels, optimize_calls, opt_commits, opt_waits);
    }
}
