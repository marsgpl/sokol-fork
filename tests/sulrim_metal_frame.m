// LLM maintained. Offscreen Metal: wait before transient CPU writes and safe native interop.
#define SOKOL_IMPL
#define SOKOL_METAL
#include "sokol_gfx.h"
#include <stdio.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "failed: %s:%d\n", #c, __LINE__); abort(); } } while (0)

static void fail_log(const char* tag, uint32_t level, uint32_t item, const char* msg,
    uint32_t line, const char* file, void* data) {
    (void)tag; (void)item; (void)file; (void)data;
    if (level <= 1) { fprintf(stderr, "%u: %s\n", line, msg); abort(); }
}

int main(void) {
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        CHECK(device);
        sg_setup(&(sg_desc){.environment.metal.device = (__bridge const void*)device, .logger.func = fail_log});
        sg_buffer buffer = sg_make_buffer(&(sg_buffer_desc){
            .size = 16, .usage = {.write_transient = true, .copy_src = true},
        });
        sg_image image = sg_make_image(&(sg_image_desc){
            .width = 2, .height = 2, .pixel_format = SG_PIXELFORMAT_RGBA8,
            .usage = {.write_transient = true, .copy_src = true},
        });
        CHECK(sg_query_buffer_state(buffer) == SG_RESOURCESTATE_VALID);
        CHECK(sg_query_image_state(image) == SG_RESOURCESTATE_VALID);
        for (int role = 0; role < 3; role++) {
            CHECK(!sg_mtl_command_buffer());
            dispatch_semaphore_t previous = _sg.mtl.sem;
            dispatch_semaphore_t gate = dispatch_semaphore_create(0);
            dispatch_semaphore_t entered = dispatch_semaphore_create(0);
            dispatch_semaphore_t done = dispatch_semaphore_create(0);
            _sg.mtl.sem = gate;
            dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
                @autoreleasepool {
                    dispatch_semaphore_signal(entered);
                    uint32_t pixels[4] = {11, 22, 33, 44};
                    if (role == 0) {
                        sg_write_buffer_transient(&(sg_write_buffer_desc){
                            .src.data = SG_RANGE(pixels), .dst.buffer = buffer,
                        });
                    } else if (role == 1) {
                        sg_write_image_transient(&(sg_write_image_desc){
                            .src.data = SG_RANGE(pixels), .dst.image = image,
                        });
                    } else {
                        sg_mtl_begin_frame();
                    }
                    CHECK(sg_mtl_command_buffer());
                    // A second acquisition must reuse the token, not wait again.
                    sg_mtl_begin_frame();
                    CHECK(sg_mtl_command_buffer());
                    id<MTLCommandBuffer> commands = (__bridge id<MTLCommandBuffer>)sg_mtl_command_buffer();
                    sg_commit();
                    [commands waitUntilCompleted];
                    CHECK(commands.status == MTLCommandBufferStatusCompleted);
                    dispatch_semaphore_signal(done);
                }
            });
            CHECK(dispatch_semaphore_wait(entered, dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC)) == 0);
            CHECK(dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 20 * NSEC_PER_MSEC)) != 0);
            dispatch_semaphore_signal(gate);
            CHECK(dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC)) == 0);
            CHECK(dispatch_semaphore_wait(gate, DISPATCH_TIME_NOW) == 0);
            _sg.mtl.sem = previous;
            #if !__has_feature(objc_arc)
            dispatch_release(gate); dispatch_release(entered); dispatch_release(done);
            #endif
        }
        uint32_t values[4] = {5, 7, 9, 13};
        sg_buffer source = sg_make_buffer(&(sg_buffer_desc){.data = SG_RANGE(values), .usage.copy_src = true});
        sg_buffer target = sg_make_buffer(&(sg_buffer_desc){.size = sizeof values, .usage.copy_dst = true});
        sg_copy_buffer_to_buffer(&(sg_copy_buffer_to_buffer_desc){
            .src.buffer = source, .dst.buffer = target, .size = sizeof values,
        });
        CHECK(_sg.mtl.blit_cmd_encoder);
        id<MTLCommandBuffer> commands = (__bridge id<MTLCommandBuffer>)sg_mtl_command_buffer();
        CHECK(!_sg.mtl.blit_cmd_encoder);
        id<MTLBlitCommandEncoder> external = [commands blitCommandEncoder];
        CHECK(external);
        sg_mtl_buffer_info info = sg_mtl_query_buffer_info(target);
        id<MTLBuffer> native = (__bridge id<MTLBuffer>)info.buf[info.active_slot];
        CHECK(native);
        [external endEncoding];
        sg_commit();
        [commands waitUntilCompleted];
        CHECK(commands.status == MTLCommandBufferStatusCompleted);
        CHECK(memcmp([native contents], values, sizeof values) == 0);
        sg_shutdown();
        _SG_OBJC_RELEASE(device);
        puts("Metal frame: buffer/image CPU writes and explicit acquisition wait; copy/native interop: PASS");
    }
}
