// Native mocks verify ownership and submission order, not GPU execution validity.
#define SOKOL_IMPL
#define SOKOL_WGPU
#include "sokol_gfx.h"
#include <stdio.h>
#include <setjmp.h>

struct WGPUBufferImpl { int refs, destroyed; uint64_t size; uint8_t data[256]; };
struct WGPUTextureImpl { int refs, destroyed; };
struct WGPUTextureViewImpl { WGPUTexture texture; };
struct WGPUCommandEncoderImpl {
    WGPUBuffer src, dst;
    WGPUTexture texture;
    bool is_finished;
};
static struct {
    struct WGPUBufferImpl buffers[1024];
    struct WGPUTextureImpl textures[256];
    struct WGPUTextureViewImpl views[256];
    struct WGPUCommandEncoderImpl encoders[256];
    unsigned buffers_n, textures_n, views_n, encoders_n, submits, index_binds, group_releases;
    WGPUIndexFormat index_format;
    uint32_t offsets[SG_MAX_UNIFORMBLOCK_BINDSLOTS];
    size_t offsets_n;
    bool is_create_failure;
} mock;

WGPUBuffer wgpuDeviceCreateBuffer(WGPUDevice device, const WGPUBufferDescriptor* desc) {
    (void)device;
    if (mock.is_create_failure) return 0;
    assert(mock.buffers_n < 1024);
    WGPUBuffer b = &mock.buffers[mock.buffers_n++];
    b->refs = 1; b->size = desc->size;
    return b;
}
void wgpuBufferAddRef(WGPUBuffer b) { assert(b->refs > 0); b->refs++; }
void wgpuBufferRelease(WGPUBuffer b) { assert(b->refs > 0); b->refs--; }
void wgpuBufferDestroy(WGPUBuffer b) {
    assert(!b->destroyed && b->refs > 0);
    for (unsigned i = 0; i < mock.encoders_n; i++) {
        assert(mock.encoders[i].src != b && mock.encoders[i].dst != b);
    }
    b->destroyed++;
}
void* wgpuBufferGetMappedRange(WGPUBuffer b, size_t offset, size_t size) {
    assert(offset + size <= sizeof b->data); return b->data + offset;
}
void wgpuBufferUnmap(WGPUBuffer b) { (void)b; }
void wgpuQueueWriteBuffer(WGPUQueue queue, WGPUBuffer b, uint64_t offset, const void* data, size_t size) {
    (void)queue; (void)offset; (void)data; (void)size; assert(!b->destroyed);
}
WGPUTexture wgpuDeviceCreateTexture(WGPUDevice device, const WGPUTextureDescriptor* desc) {
    (void)device; (void)desc;
    if (mock.is_create_failure) return 0;
    assert(mock.textures_n < 256);
    WGPUTexture t = &mock.textures[mock.textures_n++]; t->refs = 1; return t;
}
void wgpuTextureAddRef(WGPUTexture t) { assert(t->refs > 0); t->refs++; }
void wgpuTextureRelease(WGPUTexture t) { assert(t->refs > 0); t->refs--; }
void wgpuTextureDestroy(WGPUTexture t) {
    assert(!t->destroyed && t->refs > 0);
    for (unsigned i = 0; i < mock.encoders_n; i++) assert(mock.encoders[i].texture != t);
    t->destroyed++;
}
void wgpuQueueWriteTexture(WGPUQueue queue, const WGPUTexelCopyTextureInfo* dst, const void* data, size_t size,
                          const WGPUTexelCopyBufferLayout* layout, const WGPUExtent3D* extent) {
    (void)queue; (void)data; (void)size; (void)layout; (void)extent; assert(!dst->texture->destroyed);
}
WGPUTextureView wgpuTextureCreateView(WGPUTexture t, const WGPUTextureViewDescriptor* desc) {
    (void)desc; assert(!t->destroyed); assert(mock.views_n < 256);
    WGPUTextureView v = &mock.views[mock.views_n++]; v->texture = t; return v;
}
void wgpuTextureViewRelease(WGPUTextureView v) { (void)v; }
WGPUCommandEncoder wgpuDeviceCreateCommandEncoder(WGPUDevice device, const WGPUCommandEncoderDescriptor* desc) {
    (void)device; (void)desc; assert(mock.encoders_n < 256); return &mock.encoders[mock.encoders_n++];
}
void wgpuCommandEncoderCopyBufferToBuffer(WGPUCommandEncoder enc, WGPUBuffer src, uint64_t so,
                                        WGPUBuffer dst, uint64_t d, uint64_t size) {
    assert(!src->destroyed && !dst->destroyed && so == 0 && d == 0 && size == 16);
    enc->src = src; enc->dst = dst;
}
WGPUCommandBuffer wgpuCommandEncoderFinish(WGPUCommandEncoder enc, const WGPUCommandBufferDescriptor* desc) {
    (void)desc; enc->is_finished = true; return (WGPUCommandBuffer)enc;
}
void wgpuCommandEncoderRelease(WGPUCommandEncoder enc) {
    if (!enc->is_finished) { enc->src = enc->dst = 0; enc->texture = 0; }
}
void wgpuCommandBufferRelease(WGPUCommandBuffer command) { (void)command; }
void wgpuQueueSubmit(WGPUQueue queue, size_t count, const WGPUCommandBuffer* commands) {
    (void)queue; mock.submits++;
    for (size_t i = 0; i < count; i++) {
        WGPUCommandEncoder enc = (WGPUCommandEncoder)commands[i];
        if (enc->src) {
            assert(!enc->src->destroyed && !enc->dst->destroyed);
            memcpy(enc->dst->data, enc->src->data, 16);
        }
        if (enc->texture) assert(!enc->texture->destroyed);
        enc->src = enc->dst = 0; enc->texture = 0;
    }
}
void wgpuQueueRelease(WGPUQueue queue) { (void)queue; }
void wgpuRenderPassEncoderRelease(WGPURenderPassEncoder pass) { (void)pass; }
void wgpuComputePassEncoderRelease(WGPUComputePassEncoder pass) { (void)pass; }
void wgpuRenderPassEncoderSetIndexBuffer(WGPURenderPassEncoder pass, WGPUBuffer b, WGPUIndexFormat format, uint64_t offset, uint64_t size) {
    (void)pass; (void)offset; (void)size; assert(!b->destroyed); mock.index_binds++; mock.index_format = format;
}
WGPUShaderModule wgpuDeviceCreateShaderModule(WGPUDevice device, const WGPUShaderModuleDescriptor* desc) { (void)device; (void)desc; return (WGPUShaderModule)1; }
void wgpuShaderModuleRelease(WGPUShaderModule module) { (void)module; }
WGPUBindGroupLayout wgpuDeviceCreateBindGroupLayout(WGPUDevice device, const WGPUBindGroupLayoutDescriptor* desc) { (void)device; (void)desc; return (WGPUBindGroupLayout)1; }
void wgpuBindGroupLayoutRelease(WGPUBindGroupLayout layout) { (void)layout; }
WGPUBindGroup wgpuDeviceCreateBindGroup(WGPUDevice device, const WGPUBindGroupDescriptor* desc) { (void)device; (void)desc; return (WGPUBindGroup)1; }
void wgpuBindGroupRelease(WGPUBindGroup bg) { (void)bg; mock.group_releases++; }
void wgpuRenderPipelineRelease(WGPURenderPipeline pip) { (void)pip; }
void wgpuComputePipelineRelease(WGPUComputePipeline pip) { (void)pip; }
void wgpuSamplerRelease(WGPUSampler sampler) { (void)sampler; }
void wgpuRenderPassEncoderSetBindGroup(WGPURenderPassEncoder pass, uint32_t index, WGPUBindGroup group, size_t count, const uint32_t* offsets) {
    (void)pass; (void)index; (void)group; mock.offsets_n = count;
    if (count) memcpy(mock.offsets, offsets, count * sizeof(*offsets));
}
void wgpuComputePassEncoderSetBindGroup(WGPUComputePassEncoder pass, uint32_t index, WGPUBindGroup group, size_t count, const uint32_t* offsets) {
    wgpuRenderPassEncoderSetBindGroup((WGPURenderPassEncoder)pass, index, group, count, offsets);
}

static void setup(unsigned capacity) {
    memset(&_sg, 0, sizeof _sg); memset(&mock, 0, sizeof mock);
    _sg.valid = true; _sg.stats_enabled = true; _sg.frame_index = 1;
    _sg.desc = _sg_desc_defaults(&(sg_desc){.disable_validation = true, .buffer_pool_size = 8, .image_pool_size = 4,
        .wgpu = {.retirement_queue_size = (int)capacity, .bindgroups_cache_size = 8},
        .environment.defaults = {.color_format = SG_PIXELFORMAT_RGBA8, .depth_format = SG_PIXELFORMAT_DEPTH, .sample_count = 1}});
    _sg_setup_pools(&_sg.pools, &_sg.desc);
    _sg_setup_commit_listeners(&_sg.desc);
    _sg.wgpu.dev = (WGPUDevice)1; _sg.wgpu.queue = (WGPUQueue)1; _sg.wgpu.valid = true;
    _sg.wgpu.limits.minUniformBufferOffsetAlignment = 256;
    _sg_wgpu_uniform_system_init(&_sg.desc);
    _sg_wgpu_bindgroups_pool_init(&_sg.desc);
    _sg_wgpu_bindgroups_cache_init(&_sg.desc);
    _sg_wgpu_retirement_init(&_sg.desc);
}
static sg_buffer buffer(void) { return sg_make_buffer(&(sg_buffer_desc){.size = 16, .usage.dynamic_update = true}); }
static WGPUBuffer native(sg_buffer b) { return (WGPUBuffer)sg_wgpu_query_buffer_info(b).buf; }
static sg_image texture(void) { return sg_make_image(&(sg_image_desc){.width = 8, .height = 8, .usage.dynamic_update = true, .pixel_format = SG_PIXELFORMAT_RGBA8}); }
static void begin(void) { assert(!_sg.wgpu.cmd_enc); _sg.wgpu.cmd_enc = wgpuDeviceCreateCommandEncoder(_sg.wgpu.dev, 0); }
static void check_shutdown(void) {
    sg_shutdown();
    for (unsigned i = 0; i < mock.buffers_n; i++) assert(mock.buffers[i].refs == 0 && mock.buffers[i].destroyed == 1);
    for (unsigned i = 0; i < mock.textures_n; i++) assert(mock.textures[i].refs == 0 && mock.textures[i].destroyed == 1);
}

static void test_copy_and_reuse(void) {
    setup(8);
    sg_buffer src = buffer(), dst = buffer();
    WGPUBuffer old = native(src), copied = native(dst);
    memset(old->data, 0x51, 16);
    begin();
    wgpuCommandEncoderCopyBufferToBuffer(_sg.wgpu.cmd_enc, old, 0, copied, 0, 16);
    sg_destroy_buffer(src);
    sg_buffer replacement = buffer();
    assert(_sg_slot_index(src.id) == _sg_slot_index(replacement.id) && src.id != replacement.id);
    assert(native(replacement) != old && !old->destroyed);
    sg_destroy_buffer(src); // stale public handle must not touch the replacement
    assert(sg_query_stats().wgpu_memory.retired_count == 1);
    assert(sg_query_stats().wgpu_memory.retired_unsubmitted_bytes == 16);
    sg_commit();
    assert(mock.submits == 1 && old->destroyed == 1 && !native(replacement)->destroyed);
    assert(copied->data[0] == 0x51);
    sg_wgpu_memory_stats m = sg_query_stats().wgpu_memory;
    assert(m.retired_count == 0 && m.retired_buffer_bytes == 0 && m.retired_bytes_peak == 16);
    assert(m.owned_buffers_created == 3 && m.owned_buffers_destroyed == 1);
    // Uninit/reinit also reuses the public ID; cached vertex/index state must be invalidated.
    WGPUBuffer previous = native(replacement);
    _sg_wgpu_bindings_cache_ib_update(_sg_lookup_buffer(replacement.id), 0, WGPUIndexFormat_Uint16);
    _sg_wgpu_bindings_cache_vb_update(0, _sg_lookup_buffer(replacement.id), 0);
    sg_uninit_buffer(replacement);
    sg_init_buffer(replacement, &(sg_buffer_desc){.size = 16, .usage.dynamic_update = true});
    assert(_sg.wgpu.bindings_cache.ib.buffer.id == 0 && _sg.wgpu.bindings_cache.vbs[0].buffer.id == 0);
    assert(previous != native(replacement));
    sg_commit(); // no pass or extra submit
    assert(mock.submits == 1 && previous->destroyed == 1);
    check_shutdown();
}

static void test_borrowed_failed_and_readback(void) {
    setup(8);
    WGPUBuffer external = wgpuDeviceCreateBuffer(_sg.wgpu.dev, &(WGPUBufferDescriptor){.size = 16});
    WGPUTexture external_image = wgpuDeviceCreateTexture(_sg.wgpu.dev, &(WGPUTextureDescriptor){0});
    sg_buffer b = sg_make_buffer(&(sg_buffer_desc){.size = 16, .wgpu_buffer = external});
    sg_image t = sg_make_image(&(sg_image_desc){.width = 8, .height = 8, .wgpu_texture = external_image});
    sg_destroy_buffer(b); sg_destroy_image(t); sg_commit();
    assert(external->refs == 1 && !external->destroyed && external_image->refs == 1 && !external_image->destroyed);
    assert(sg_query_stats().wgpu_memory.owned_buffers_created == 0 && sg_query_stats().wgpu_memory.owned_images_created == 0);
    wgpuBufferDestroy(external); wgpuBufferRelease(external);
    wgpuTextureDestroy(external_image); wgpuTextureRelease(external_image);
    mock.is_create_failure = true;
    b = buffer(); t = texture();
    assert(sg_query_buffer_state(b) == SG_RESOURCESTATE_FAILED && sg_query_image_state(t) == SG_RESOURCESTATE_FAILED);
    sg_destroy_buffer(b); sg_destroy_image(t);
    assert(sg_query_stats().wgpu_memory.retired_count == 0);
    mock.is_create_failure = false;
    // A submitted copy owns its destination data; a pending callback can retain the destroyed source handle.
    b = buffer(); sg_buffer dst = buffer(); WGPUBuffer source = native(b), staging = native(dst);
    source->data[0] = 0x73; wgpuBufferAddRef(source);
    begin();
    wgpuCommandEncoderCopyBufferToBuffer(_sg.wgpu.cmd_enc, source, 0, staging, 0, 16);
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(_sg.wgpu.dev, 0);
    wgpuCommandEncoderCopyBufferToBuffer(enc, source, 0, staging, 0, 16);
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(enc, 0); wgpuQueueSubmit(_sg.wgpu.queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd); wgpuCommandEncoderRelease(enc);
    sg_destroy_buffer(b);
    assert(!source->destroyed); // the helper submit cannot retire the still-unsubmitted frame use
    sg_commit();
    assert(source->destroyed && source->refs == 1 && staging->data[0] == 0x73);
    wgpuBufferRelease(source);
    check_shutdown();
}

static void test_capacity_budget_and_shutdown(void) {
    setup(128); sg_disable_stats();
    begin();
    for (unsigned i = 0; i < 128; i++) sg_destroy_buffer(buffer());
    sg_wgpu_memory_stats m = sg_query_stats().wgpu_memory;
    assert(m.retired_count == 128 && m.retired_count_peak == 128 && m.retired_buffer_bytes == 128 * 16);
    assert(m.retirement_queue_bytes == 128 * sizeof(_sg_wgpu_retired_t));
    sg_commit();
    assert(mock.submits == 1 && sg_query_stats().wgpu_memory.retired_count == 64);
    assert(sg_query_stats().wgpu_memory.retired_unsubmitted_bytes == 0);
    sg_commit();
    assert(mock.submits == 1 && sg_query_stats().wgpu_memory.retired_count == 0);
    // Wrap the fixed queue repeatedly; stats-off lifetimes and accounting stay correct.
    for (unsigned cycle = 0; cycle < 3; cycle++) {
        begin();
        for (unsigned i = 0; i < 128; i++) sg_destroy_buffer(buffer());
        sg_commit(); sg_commit();
        assert(sg_query_stats().wgpu_memory.retired_count == 0);
    }
    // Shutdown/device-loss teardown abandons the encoder without another submit or callback.
    sg_buffer a = buffer(), b = buffer(); begin();
    wgpuCommandEncoderCopyBufferToBuffer(_sg.wgpu.cmd_enc, native(a), 0, native(b), 0, 16);
    sg_destroy_buffer(a);
    unsigned submits = mock.submits;
    check_shutdown(); assert(mock.submits == submits);
}

static jmp_buf panic_target;
static void panic_logger(const char* tag, uint32_t level, uint32_t item, const char* msg, uint32_t line, const char* filename, void* data) {
    (void)tag; (void)msg; (void)line; (void)filename; (void)data;
    assert(level == 0 && item == SG_LOGITEM_WGPU_RETIREMENT_QUEUE_FULL); longjmp(panic_target, 1);
}
static void test_full_queue(void) {
    setup(1); begin();
    sg_buffer a = buffer(), b = buffer();
    sg_destroy_buffer(a);
    _sg.desc.logger.func = panic_logger;
    if (setjmp(panic_target) == 0) { sg_destroy_buffer(b); assert(false); }
    _sg.desc.logger.func = 0;
    assert(sg_query_stats().wgpu_memory.retired_count == 1);
    assert(sg_query_stats().wgpu_memory.retired_unsubmitted_bytes == 16);
    sg_commit(); // the failed admission kept the still-live handle
    check_shutdown();
}

static void test_views_and_images(void) {
    setup(8);
    sg_image img = texture(); WGPUTexture old = (WGPUTexture)sg_wgpu_query_image_info(img).tex;
    sg_view v = sg_make_view(&(sg_view_desc){.texture.image = img});
    sg_view v2 = sg_make_view(&(sg_view_desc){.texture.image = img});
    sg_view v3 = sg_make_view(&(sg_view_desc){.texture.image = img});
    sg_destroy_view(v2); // unlink the middle of the parent's list
    _sg_view_t* view = _sg_lookup_view(v.id);
    _sg_wgpu_bindgroup_handle_t id = _sg_wgpu_alloc_bindgroup();
    _sg_wgpu_bindgroup_t* bg = _sg_wgpu_lookup_bindgroup(id.id);
    bg->slot.state = SG_RESOURCESTATE_VALID; bg->bindgroup = (WGPUBindGroup)1;
    bg->key.items[1] = _sg_wgpu_bindgroups_cache_view_item(0, &view->slot);
    _sg_wgpu_bindgroups_cache_set(1, id.id); _sg.wgpu.bindings_cache.bg = id;
    begin(); _sg.wgpu.cmd_enc->texture = old;
    sg_uninit_image(img);
    assert(_sg_wgpu_bindgroups_cache_get(1) == SG_INVALID_ID && _sg.wgpu.bindings_cache.bg.id == SG_INVALID_ID);
    assert(mock.group_releases == 1 && !old->destroyed);
    assert(sg_query_stats().wgpu_memory.retired_image_bytes == 256);
    sg_init_image(img, &(sg_image_desc){.width = 8, .height = 8, .usage.dynamic_update = true});
    sg_view replacement = sg_make_view(&(sg_view_desc){.texture.image = img});
    sg_destroy_view(v); sg_destroy_view(v3); // stale views cannot unlink a replacement generation
    assert(_sg_lookup_image(img.id)->wgpu.views == _sg_lookup_view(replacement.id));
    sg_commit(); assert(old->destroyed == 1);
    // Byte estimates include shrinking 3D mips, array/cube slices, samples and compressed blocks.
    _sg_image_t sized = {0};
    sized.cmn = (_sg_image_common_t){.type = SG_IMAGETYPE_3D, .width = 8, .height = 4, .num_slices = 4,
        .num_mipmaps = 3, .sample_count = 1, .pixel_format = SG_PIXELFORMAT_RGBA8};
    assert(_sg_wgpu_image_bytes(&sized) == 584);
    sized.cmn.type = SG_IMAGETYPE_CUBE; sized.cmn.num_slices = 6;
    assert(_sg_wgpu_image_bytes(&sized) == 1008);
    sized.cmn.sample_count = 4; assert(_sg_wgpu_image_bytes(&sized) == 4032);
    sized.cmn = (_sg_image_common_t){.type = SG_IMAGETYPE_2D, .width = 7, .height = 5, .num_slices = 1,
        .num_mipmaps = 1, .sample_count = 1, .pixel_format = SG_PIXELFORMAT_BC1_RGBA};
    assert(_sg_wgpu_image_bytes(&sized) == 32);
    sized.cmn.width = sized.cmn.height = 16384; sized.cmn.pixel_format = SG_PIXELFORMAT_RGBA32F;
    assert(_sg_wgpu_image_bytes(&sized) == UINT64_C(4294967296));
    sg_buffer storage = sg_make_buffer(&(sg_buffer_desc){.size = 16, .usage.storage_buffer = true, .usage.dynamic_update = true});
    sg_view sv = sg_make_view(&(sg_view_desc){.storage_buffer.buffer = storage});
    _sg_view_t* storage_view = _sg_lookup_view(sv.id);
    id = _sg_wgpu_alloc_bindgroup(); bg = _sg_wgpu_lookup_bindgroup(id.id);
    bg->slot.state = SG_RESOURCESTATE_VALID; bg->bindgroup = (WGPUBindGroup)1;
    bg->key.items[1] = _sg_wgpu_bindgroups_cache_view_item(0, &storage_view->slot);
    _sg_wgpu_bindgroups_cache_set(1, id.id);
    sg_destroy_buffer(storage);
    assert(_sg_wgpu_bindgroups_cache_get(1) == SG_INVALID_ID);
    assert(!_sg_buffer_ref_valid(&storage_view->cmn.buf.ref));
    sg_destroy_view(sv);
    check_shutdown();
}

static void test_sparse_uniforms_and_index_format(void) {
    setup(8);
    _sg_shader_t shd = {0}; shd.slot.id = 1;
    shd.cmn.uniform_blocks[2].stage = SG_SHADERSTAGE_VERTEX;
    shd.cmn.uniform_blocks[5].stage = SG_SHADERSTAGE_FRAGMENT;
    sg_shader_desc desc = {0}; desc.uniform_blocks[2].wgsl_group0_binding_n = 7; desc.uniform_blocks[5].wgsl_group0_binding_n = 1;
    assert(_sg_wgpu_create_shader(&shd, &desc) == SG_RESOURCESTATE_VALID);
    assert(shd.wgpu.ub_num_dynoffsets == 2 && shd.wgpu.ub_dynoffsets[2] == 1 && shd.wgpu.ub_dynoffsets[5] == 0);
    _sg_pipeline_t pip = {0}; pip.slot.id = 1; pip.cmn.shader = _sg_shader_ref(&shd);
    _sg.cur_pip = _sg_pipeline_ref(&pip); _sg.wgpu.rpass_enc = (WGPURenderPassEncoder)1;
    _sg.wgpu.uniform.bind_offsets[2] = 256; _sg.wgpu.uniform.bind_offsets[5] = 512; _sg.wgpu.uniform.dirty = true;
    _sg_wgpu_uniform_system_set_bindgroup();
    assert(mock.offsets_n == 2 && mock.offsets[0] == 512 && mock.offsets[1] == 256);
    sg_buffer b = buffer();
    _sg_bindings_ptrs_t bnd = {.pip = &pip, .ib = _sg_lookup_buffer(b.id)};
    pip.cmn.index_type = SG_INDEXTYPE_UINT16; _sg_wgpu_apply_index_buffer(&bnd); _sg_wgpu_apply_index_buffer(&bnd);
    assert(mock.index_binds == 1 && mock.index_format == WGPUIndexFormat_Uint16);
    pip.cmn.index_type = SG_INDEXTYPE_UINT32; _sg_wgpu_apply_index_buffer(&bnd);
    assert(mock.index_binds == 2 && mock.index_format == WGPUIndexFormat_Uint32);
    _sg.wgpu.rpass_enc = 0; _sg_wgpu_discard_shader(&shd);
    check_shutdown();
}
int main(void) {
    test_copy_and_reuse(); test_borrowed_failed_and_readback(); test_capacity_budget_and_shutdown();
    test_full_queue(); test_views_and_images(); test_sparse_uniforms_and_index_format();
    puts("Sokol WebGPU P1: 6 lifetime/correctness tests passed");
}
