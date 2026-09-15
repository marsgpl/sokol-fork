// Non-ARC ownership/error-path mocks. No window, shader compiler or GPU execution.
#define SOKOL_IMPL
#define SOKOL_METAL
#include "sokol_gfx.h"
#include <stdio.h>

static unsigned deallocations;
@interface SlopaMetalMock : NSObject {
@public
    BOOL is_view_failure;
}
@end
@implementation SlopaMetalMock
- (void)dealloc { deallocations++; [super dealloc]; }
- (id)newTextureViewWithPixelFormat:(MTLPixelFormat)format textureType:(MTLTextureType)type levels:(NSRange)levels slices:(NSRange)slices {
    (void)format; (void)type; (void)levels; (void)slices;
    return is_view_failure ? nil : [[SlopaMetalMock alloc] init];
}
- (id)newLibraryWithSource:(NSString*)source options:(MTLCompileOptions*)options error:(NSError**)error {
    (void)options; (void)error;
    return [source isEqualToString:@"fail"] ? nil : [[SlopaMetalMock alloc] init];
}
- (id)newFunctionWithName:(NSString*)name { (void)name; return [[SlopaMetalMock alloc] init]; }
@end

int main(void) {
    @autoreleasepool {
        _sg.frame_index = 1;
        _sg.desc = _sg_desc_defaults(&(sg_desc){.buffer_pool_size = 4, .image_pool_size = 4,
            .environment.defaults = {.color_format = SG_PIXELFORMAT_RGBA8, .depth_format = SG_PIXELFORMAT_DEPTH, .sample_count = 1}});
        _sg_mtl_init_pool(&_sg.desc);
        SlopaMetalMock* device = [[SlopaMetalMock alloc] init];
        _sg.mtl.device = (id<MTLDevice>)device;
        SlopaMetalMock* b = [[SlopaMetalMock alloc] init];
        SlopaMetalMock* t = [[SlopaMetalMock alloc] init];
        SlopaMetalMock* sampler = [[SlopaMetalMock alloc] init];
        _sg_buffer_t buf = {0}; buf.cmn.size = 16; buf.cmn.num_slots = 1;
        _sg_image_t img = {0}; img.slot.id = 1; img.slot.state = SG_RESOURCESTATE_VALID;
        img.cmn = (_sg_image_common_t){.type = SG_IMAGETYPE_2D, .width = 8, .height = 8, .num_slices = 1,
            .num_mipmaps = 1, .sample_count = 1, .num_slots = 1, .pixel_format = SG_PIXELFORMAT_RGBA8};
        _sg_sampler_t smp = {0};
        assert(_sg_mtl_create_buffer(&buf, &(sg_buffer_desc){.mtl_buffers[0] = b}) == SG_RESOURCESTATE_VALID);
        assert(_sg_mtl_create_image(&img, &(sg_image_desc){.mtl_textures[0] = t}) == SG_RESOURCESTATE_VALID);
        assert(_sg_mtl_create_sampler(&smp, &(sg_sampler_desc){.mtl_sampler = sampler}) == SG_RESOURCESTATE_VALID);
        [b release]; [t release]; [sampler release];
        assert(deallocations == 0); // the pool, independently of the external owner, retains all three
        t->is_view_failure = YES;
        _sg_view_t view = {0}; view.cmn.type = SG_VIEWTYPE_TEXTURE;
        view.cmn.img.ref = _sg_image_ref(&img); view.cmn.img.mip_level_count = view.cmn.img.slice_count = 1;
        assert(_sg_mtl_create_view(&view, &(sg_view_desc){0}) == SG_RESOURCESTATE_FAILED);
        _sg_mtl_discard_view(&view); // a failed view owns no pool object
        assert(_sg.mtl.idpool.release_queue_front == 0);
        _sg_shader_t shd = {0};
        sg_shader_desc desc = {.vertex_func = {.source = "ok", .entry = "vs"}, .fragment_func = {.source = "fail", .entry = "fs"}};
        assert(_sg_mtl_create_shader(&shd, &desc) == SG_RESOURCESTATE_FAILED);
        assert(_sg.mtl.idpool.release_queue_front == 0); // no eager cleanup
        _sg_mtl_discard_shader(&shd);
        assert(_sg.mtl.idpool.release_queue_front == 2); // exactly one library + one function
        _sg_mtl_discard_buffer(&buf); _sg_mtl_discard_image(&img); _sg_mtl_discard_sampler(&smp);
        assert(_sg.mtl.idpool.release_queue_front == 5);
        _sg_mtl_destroy_pool();
        [device release]; _sg.mtl.device = nil;
    }
    assert(deallocations == 6);
    puts("Sokol Metal P1: injected ownership, failed view and partial shader cleanup passed");
}
