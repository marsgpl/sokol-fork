// Native mocks verify ownership and submission order, not GPU execution validity.
#define SOKOL_IMPL
#define SOKOL_WGPU
#include "sokol_gfx.h"
#include <stdio.h>
#include <setjmp.h>

struct WGPUBufferImpl { int refs, destroyed; uint64_t size; uint8_t data[256]; };
struct WGPUTextureImpl { int refs, destroyed; unsigned width, height, mips, block, block_bytes; uint8_t data[SG_MAX_MIPMAPS][1024]; };
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
    unsigned buffers_n, textures_n, views_n, encoders_n, submits, index_binds, group_releases, texture_writes;
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
    WGPUTexture t = &mock.textures[mock.textures_n++]; t->refs = 1;
    t->width = desc->size.width; t->height = desc->size.height; t->mips = desc->mipLevelCount;
    bool is_compressed = desc->format == WGPUTextureFormat_BC7RGBAUnorm || desc->format == WGPUTextureFormat_ASTC4x4Unorm || desc->format == WGPUTextureFormat_ETC2RGBA8Unorm;
    t->block = is_compressed ? 4 : 1; t->block_bytes = is_compressed ? 16 : 4;
    return t;
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
    (void)queue; WGPUTexture t = dst->texture;
    assert(!t->destroyed && dst->mipLevel < t->mips && extent->depthOrArrayLayers == 1);
    unsigned w = (unsigned)_sg_miplevel_dim((int)t->width, (int)dst->mipLevel);
    unsigned h = (unsigned)_sg_miplevel_dim((int)t->height, (int)dst->mipLevel);
    w = (w + t->block - 1) / t->block; h = (h + t->block - 1) / t->block;
    assert(!(extent->width % t->block) && !(extent->height % t->block));
    assert(!(dst->origin.x % t->block) && !(dst->origin.y % t->block));
    unsigned x = dst->origin.x / t->block, y0 = dst->origin.y / t->block;
    unsigned rows = extent->height / t->block, row_bytes = extent->width / t->block * t->block_bytes;
    assert(x + extent->width / t->block <= w && y0 + rows <= h && dst->origin.z == 0);
    assert(w * h * t->block_bytes <= sizeof t->data[0]);
    assert(layout->rowsPerImage >= rows && !(layout->bytesPerRow % t->block_bytes));
    assert(layout->offset + (rows - 1) * layout->bytesPerRow + row_bytes <= size);
    for (unsigned y = 0; y < rows; ++y) {
        memcpy(t->data[dst->mipLevel] + ((y0 + y) * w + x) * t->block_bytes,
               (const uint8_t*)data + layout->offset + y * layout->bytesPerRow, row_bytes);
    }
    mock.texture_writes++;
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
static void test_unsealed_images(void) {
    setup(8);
    const sg_image_desc desc = {.width = 8, .height = 4, .num_mipmaps = 4, .usage.write_unsealed = true};
    sg_image img = sg_make_image(&desc);
    WGPUTexture texture = (WGPUTexture)sg_wgpu_query_image_info(img).tex;
    assert(sg_query_image_state(img) == SG_RESOURCESTATE_UNSEALED && mock.texture_writes == 0);
    // Even with debug validation disabled, no view can expose an unsealed texture.
    sg_view view = sg_make_view(&(sg_view_desc){.texture.image = img});
    assert(sg_query_view_state(view) == SG_RESOURCESTATE_FAILED);
    sg_destroy_view(view);
    uint8_t data[256];
    for (unsigned i = 0; i < sizeof data; ++i) data[i] = (uint8_t)i;
    sg_write_image_unsealed(&(sg_write_image_desc){.src.data = {data, 128}, .dst.image = img});
    assert(mock.texture_writes == 1 && memcmp(texture->data[0], data, 128) == 0);
    sg_write_image_desc patch = {.src = {.data = {data, 64}, .offset = 4, .bytes_per_row = 16, .bytes_per_slice = 32},
        .dst = {.image = img, .x = 2, .y = 1}, .size = {.width = 2, .height = 2}};
    sg_write_image_unsealed(&patch);
    assert(memcmp(texture->data[0] + 40, data + 4, 8) == 0);
    assert(memcmp(texture->data[0] + 72, data + 20, 8) == 0);
    assert(texture->data[0][39] == 39 && texture->data[0][48] == 48);
    for (int mip = 1; mip < 4; ++mip) {
        size_t bytes = (size_t)_sg_miplevel_dim(8, mip) * (size_t)_sg_miplevel_dim(4, mip) * 4;
        sg_write_image_unsealed(&(sg_write_image_desc){.src.data = {data, bytes}, .dst = {.image = img, .mip_level = mip}});
        assert(memcmp(texture->data[mip], data, bytes) == 0);
    }
    unsigned before = mock.texture_writes;
    // Reject invalid shifts, negative regions, overflow, short rows and undersized source spans before the backend.
    sg_write_image_desc bad[] = {patch, patch, patch, patch, patch, patch, patch};
    bad[0].dst.mip_level = -1; bad[1].dst.mip_level = 1000;
    bad[2].dst.x = -1; bad[3].src.offset = SIZE_MAX;
    bad[4].src.bytes_per_row = 4; bad[5].src.data.size = 27;
    bad[6].size.num_slices = 2;
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; ++i) sg_write_image_unsealed(&bad[i]);
    assert(mock.texture_writes == before);
    sg_seal_image(img);
    assert(sg_query_image_state(img) == SG_RESOURCESTATE_VALID);
    view = sg_make_view(&(sg_view_desc){.texture.image = img});
    assert(sg_query_view_state(view) == SG_RESOURCESTATE_VALID);
    sg_write_image_unsealed(&patch);
    assert(mock.texture_writes == before); // sealed writes stay rejected without validation
    sg_destroy_view(view); sg_destroy_image(img); sg_commit();
    sg_frame_stats stats = sg_query_stats().prev_frame;
    assert(stats.wgpu.transfers.num_write_texture == 5);
    assert(stats.wgpu.transfers.size_write_texture == 188 && stats.wgpu.transfers.size_write_padding == 8);
    assert(texture->destroyed && texture->refs == 0);

    // Partial and never-started images obey the existing retirement and slot-generation contract.
    img = sg_make_image(&desc); texture = (WGPUTexture)sg_wgpu_query_image_info(img).tex;
    begin(); sg_destroy_image(img);
    assert(!texture->destroyed);
    sg_image replacement = sg_make_image(&desc);
    patch.dst.image = img; sg_write_image_unsealed(&patch); sg_seal_image(img);
    assert(mock.texture_writes == before && sg_query_image_state(replacement) == SG_RESOURCESTATE_UNSEALED);
    sg_commit(); assert(texture->destroyed);
    sg_uninit_image(replacement);
    assert(sg_query_image_state(replacement) == SG_RESOURCESTATE_ALLOC);
    sg_init_image(replacement, &desc);
    assert(sg_query_image_state(replacement) == SG_RESOURCESTATE_UNSEALED);
    // Unsupported formats cannot enter the partial-write path, even with validation disabled.
    sg_image_desc compressed = desc; compressed.pixel_format = SG_PIXELFORMAT_BC1_RGBA;
    img = sg_make_image(&compressed); assert(sg_query_image_state(img) == SG_RESOURCESTATE_FAILED); sg_destroy_image(img);
    compressed = desc; compressed.usage.dynamic_update = true;
    img = sg_make_image(&compressed); assert(sg_query_image_state(img) == SG_RESOURCESTATE_FAILED); sg_destroy_image(img);
    compressed = desc; compressed.data.mip_levels[0] = (sg_range){data, 128};
    img = sg_make_image(&compressed); assert(sg_query_image_state(img) == SG_RESOURCESTATE_FAILED); sg_destroy_image(img);
    check_shutdown(); // leaves one unsealed image alive for shutdown to collect
}

static void test_unsealed_compressed_images(void) {
    const sg_pixel_format formats[] = {SG_PIXELFORMAT_BC7_RGBA, SG_PIXELFORMAT_ASTC_4x4_RGBA, SG_PIXELFORMAT_ETC2_RGBA8};
    for (unsigned f = 0; f < 3; ++f) {
        setup(8);
        sg_image_desc desc = {.width = 12, .height = 20, .num_mipmaps = 5, .pixel_format = formats[f], .usage.write_unsealed = true};
        sg_image img = sg_make_image(&desc);
        assert(sg_query_image_state(img) == SG_RESOURCESTATE_UNSEALED);
        WGPUTexture texture = (WGPUTexture)sg_wgpu_query_image_info(img).tex;
        uint8_t data[512];
        for (unsigned i = 0; i < sizeof data; ++i) data[i] = (uint8_t)(i * 17 + i / 7);
        uint64_t payload = 0;
        for (int mip = 0; mip < 5; ++mip) {
            int w = _sg_miplevel_dim(12, mip), h = _sg_miplevel_dim(20, mip);
            size_t bytes = (size_t)((w + 3) / 4) * ((h + 3) / 4) * 16;
            sg_write_image_unsealed(&(sg_write_image_desc){.src.data = {data, bytes}, .dst = {.image = img, .mip_level = mip}});
            assert(memcmp(texture->data[mip], data, bytes) == 0);
            payload += bytes;
        }
        // Padded two-block-row subrect, then an odd edge subrect at the 6x10 mip.
        sg_write_image_desc patch = {.src = {.data = {data, 96}, .offset = 16, .bytes_per_row = 48, .bytes_per_slice = 96},
            .dst = {.image = img, .x = 4, .y = 4}, .size = {.width = 8, .height = 8}};
        sg_write_image_unsealed(&patch);
        assert(memcmp(texture->data[0] + 64, data + 16, 32) == 0);
        assert(memcmp(texture->data[0] + 112, data + 64, 32) == 0);
        sg_write_image_desc edge = {.src.data = {data, 16}, .src.bytes_per_row = 16,
            .dst = {.image = img, .mip_level = 1, .x = 4, .y = 8}};
        sg_write_image_unsealed(&edge);
        assert(memcmp(texture->data[1] + 80, data, 16) == 0);
        unsigned before = mock.texture_writes;
        sg_write_image_desc bad[] = {patch, patch, patch, patch, patch, patch, patch, patch, patch, patch, patch};
        bad[0].dst.x = 2; bad[1].dst.y = 1; bad[2].size.width = 6; bad[3].size.height = 5;
        bad[4].src.bytes_per_row = 33; bad[5].src.bytes_per_row = 16; bad[6].src.offset = 1;
        bad[7].src.data.size = 95; bad[8].src.bytes_per_slice = 48;
        bad[9].dst.mip_level = INT32_MAX; bad[10].size.height = INT32_MAX;
        for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; ++i) sg_write_image_unsealed(&bad[i]);
        assert(mock.texture_writes == before);
        sg_seal_image(img); sg_write_image_unsealed(&patch);
        assert(sg_query_image_state(img) == SG_RESOURCESTATE_VALID && mock.texture_writes == before);
        sg_destroy_image(img); sg_commit();
        sg_frame_stats stats = sg_query_stats().prev_frame;
        assert(stats.wgpu.transfers.num_write_texture == 7);
        assert(stats.wgpu.transfers.size_write_texture == payload + 80 && stats.wgpu.transfers.size_write_padding == 16);
        assert(texture->destroyed && !texture->refs);
        desc.width = 11;
        img = sg_make_image(&desc); assert(sg_query_image_state(img) == SG_RESOURCESTATE_FAILED); sg_destroy_image(img);
        desc.width = 12;
        img = sg_make_image(&desc); // compressed partial cancellation and stale handle
        patch.dst.image = img; sg_write_image_unsealed(&patch);
        sg_destroy_image(img); sg_write_image_unsealed(&patch);
        check_shutdown();
    }
}

int main(void) {
    test_copy_and_reuse(); test_borrowed_failed_and_readback(); test_capacity_budget_and_shutdown();
    test_full_queue(); test_views_and_images(); test_sparse_uniforms_and_index_format();
    test_unsealed_images(); test_unsealed_compressed_images();
    puts("Sokol WebGPU P1/P2: 8 lifetime, correctness and unsealed-image tests passed");
}
