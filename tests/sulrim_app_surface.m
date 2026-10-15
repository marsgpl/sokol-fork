// Simulate unavailable drawables without opening a window or starting a GPU.
#define SOKOL_NO_ENTRY
#define SOKOL_APP_IMPL
#define SOKOL_METAL
#include "sokol_app.h"
#include "sokol_gfx.h"
#define SOKOL_GLUE_IMPL
#include "sokol_glue.h"
#include <stdio.h>

@interface SulrimSurfaceMock : NSObject
@property(nonatomic, strong) id drawable;
@end
@implementation SulrimSurfaceMock
- (id)nextDrawable { return self.drawable; }
@end

int main(void) {
    @autoreleasepool {
        SulrimSurfaceMock* layer = [SulrimSurfaceMock new];
        NSObject* drawable = [NSObject new];
        _sapp.valid = true;
        _sapp.framebuffer_width = 640;
        _sapp.framebuffer_height = 480;
        _sapp.sample_count = 1;
        _sapp.macos.mtl.layer = (CAMetalLayer*)layer;
        _sapp.macos.mtl.depth_tex = (id<MTLTexture>)drawable;

        for (int frame = 0; frame < 4; frame++) {
            layer.drawable = (frame & 1) ? drawable : nil;
            sapp_swapchain app = sapp_get_swapchain();
            sg_swapchain gfx = sglue_swapchain();
            assert(app.invalid == !(frame & 1));
            assert(gfx.invalid == app.invalid);
            if (app.invalid) {
                sapp_swapchain empty_app = {.invalid = true};
                sg_swapchain empty_gfx = {.invalid = true};
                assert(memcmp(&app, &empty_app, sizeof app) == 0);
                assert(memcmp(&gfx, &empty_gfx, sizeof gfx) == 0);
            } else {
                assert(app.width == 640 && app.height == 480);
                assert(app.metal.current_drawable == (__bridge const void*)drawable);
                assert(gfx.metal.current_drawable == app.metal.current_drawable);
                assert(gfx.metal.depth_stencil_texture == app.metal.depth_stencil_texture);
            }
        }
        _sapp.macos.mtl.layer = nil;
        _sapp.macos.mtl.depth_tex = nil;
    }
    puts("Sokol Metal surface: unavailable frames skipped; following frames recover PASS");
}
