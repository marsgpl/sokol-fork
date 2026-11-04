// LLM maintained. Reuse upstream WebGPU mocks; GPU execution is checked separately.
#include "wgpu_mock.h"
#define SOKOL_IMPL
#define SOKOL_WGPU
#include "sokol_gfx.h"
#include <stdio.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "failed: %s:%d\n", #c, __LINE__); abort(); } } while (0)

static void log_message(const char* tag, uint32_t level, uint32_t item, const char* message,
    uint32_t line, const char* file, void* data) {
    (void)tag; (void)file; (void)data;
    if (item == SG_LOGITEM_WGPU_CREATE_BUFFER_FAILED || item == SG_LOGITEM_WGPU_CREATE_TEXTURE_FAILED) { return; }
    fprintf(stderr, "%u:%u %s\n", line, level, message ? message : "panic");
    if (level <= 1 && item != SG_LOGITEM_WGPU_RETIREMENT_QUEUE_FULL) { abort(); }
}

static void setup(int capacity) {
    wgpu_mock_setup();
    sg_setup(&(sg_desc){
        .buffer_pool_size = 128, .image_pool_size = 16,
        .wgpu.retirement_queue_size = capacity,
        .environment.wgpu.device = wgpu_mock_device(),
        .environment.defaults = {.color_format = SG_PIXELFORMAT_RGBA8, .depth_format = SG_PIXELFORMAT_NONE, .sample_count = 1},
        .logger.func = log_message,
    });
    sg_disable_stats();
    CHECK(sg_isvalid() && !sg_stats_enabled());
}

static void shutdown(void) {
    sg_shutdown();
    for (int i = 0; i < wgpu_mock_num_errors(); i++) { fprintf(stderr, "%s\n", wgpu_mock_error(i)); }
    CHECK(!wgpu_mock_total_errors() && !wgpu_mock_call_log_overflow());
    CHECK(!wgpu_mock_live_objects_total());
    wgpu_mock_shutdown();
}

static sg_buffer buffer(void) {
    sg_buffer b = sg_make_buffer(&(sg_buffer_desc){
        .size = 16, .usage = {.storage_buffer = true, .copy_src = true, .copy_dst = true},
    });
    CHECK(sg_query_buffer_state(b) == SG_RESOURCESTATE_VALID);
    return b;
}

static bool destroyed(const void* b) {
    wgpu_mock_buffer_info_t info;
    CHECK(wgpu_mock_buffer_info(b, &info));
    return info.destroyed;
}

static void copy(sg_buffer src, sg_buffer dst) {
    sg_copy_buffer_to_buffer(&(sg_copy_buffer_to_buffer_desc){.src.buffer = src, .dst.buffer = dst, .size = 16});
}

static void lifetimes(void) {
    setup(0);
    CHECK(sg_query_stats().wgpu_memory.retirement_capacity == 144);
    sg_buffer src = buffer(), dst = buffer();
    const void* native = sg_wgpu_query_buffer_info(src).buf;
    sg_view view = sg_make_view(&(sg_view_desc){.storage_buffer.buffer = src});
    sg_view later = sg_make_view(&(sg_view_desc){.storage_buffer.buffer = src});
    CHECK(sg_query_view_state(view) == SG_RESOURCESTATE_VALID && sg_query_view_state(later) == SG_RESOURCESTATE_VALID);
    // A retained view must evict its cached group when the parent dies.
    _sg_wgpu_bindgroup_handle_t id = _sg_wgpu_alloc_bindgroup();
    _sg_wgpu_bindgroup_t* group = _sg_wgpu_lookup_bindgroup(id.id);
    group->slot.state = SG_RESOURCESTATE_VALID;
    const _sg_slot_t* slot = &_sg_lookup_view(view.id)->slot;
    group->key.items[0] = _sg_wgpu_bindgroups_cache_item(_SG_WGPU_BINDGROUPSCACHEITEMTYPE_VIEW, 9, slot->id, slot->uninit_count);
    _sg_wgpu_bindgroups_cache_promote(_sg_wgpu_bindgroups_cache_ways(5), 0, id.id);
    _sg.wgpu.bindings_cache.bg = id;
    _sg_wgpu_bindings_cache_vb_update(0, _sg_lookup_buffer(src.id), 4);
    _sg_wgpu_bindings_cache_ib_update(_sg_lookup_buffer(src.id), 0, WGPUIndexFormat_Uint16);
    copy(src, dst);
    sg_destroy_buffer(src);
    CHECK(!destroyed(native) && wgpu_mock_ref_count(native) > 0);
    CHECK(!_sg.wgpu.bindgroups_cache.items[0].id && !_sg.wgpu.bindings_cache.bg.id);
    CHECK(!_sg.wgpu.bindings_cache.vbs[0].buffer.id && !_sg.wgpu.bindings_cache.ib.buffer.id);
    CHECK(!_sg_lookup_view(view.id)->wgpu.next && !_sg_lookup_view(later.id)->wgpu.prev);
    sg_buffer reused = buffer();
    CHECK(_sg_slot_index(reused.id) == _sg_slot_index(src.id) && reused.id != src.id);
    const void* replacement = sg_wgpu_query_buffer_info(reused).buf;
    wgpu_mock_clear_calls();
    CHECK(sg_query_stats().wgpu_memory.retired_unsubmitted_bytes == 16);
    sg_commit();
    CHECK(destroyed(native) && !destroyed(replacement));
    CHECK(wgpu_mock_find_call(WGPU_MOCK_FUNC_wgpuQueueSubmit, 0)
        < wgpu_mock_find_call(WGPU_MOCK_FUNC_wgpuBufferDestroy, 0));
    CHECK(!sg_query_stats().wgpu_memory.retired_count && !sg_query_stats().wgpu_memory.retired_unsubmitted_bytes);
    sg_destroy_view(view); sg_destroy_view(later);
    sg_destroy_buffer(reused); sg_destroy_buffer(dst); sg_commit();
    sg_wgpu_memory_stats stats = sg_query_stats().wgpu_memory;
    CHECK(stats.owned_buffers_created == 3 && stats.owned_buffers_destroyed == 3);
    // Failed, CPU staging and unsealed resources retain their own cleanup paths.
    wgpu_mock_fail_next(WGPU_MOCK_OBJ_BUFFER, 1);
    sg_buffer failed = sg_make_buffer(&(sg_buffer_desc){.size = 16, .usage.copy_dst = true});
    CHECK(sg_query_buffer_state(failed) == SG_RESOURCESTATE_FAILED);
    sg_destroy_buffer(failed);
    sg_buffer staging = sg_make_buffer(&(sg_buffer_desc){.size = 16, .usage = {.staging_buffer = true, .write_transient = true, .copy_src = true}});
    CHECK(!_sg_lookup_buffer(staging.id)->wgpu.is_owned && _sg_lookup_buffer(staging.id)->wgpu.staging_ptr);
    sg_destroy_buffer(staging);
    sg_buffer unsealed = sg_make_buffer(&(sg_buffer_desc){.size = 16, .usage.write_unsealed = true});
    const void* mapped = sg_wgpu_query_buffer_info(unsealed).buf;
    CHECK(_sg_lookup_buffer(unsealed.id)->wgpu.mapped_ptr);
    sg_destroy_buffer(unsealed); sg_commit();
    CHECK(destroyed(mapped));
    shutdown();
}

static void borrowed(void) {
    setup(0);
    WGPUBuffer raw = wgpuDeviceCreateBuffer(wgpu_mock_device(), &(WGPUBufferDescriptor){.size = 16, .usage = WGPUBufferUsage_CopyDst});
    sg_buffer b = sg_make_buffer(&(sg_buffer_desc){.size = 16, .usage.copy_dst = true, .wgpu_buffer = raw});
    WGPUTexture tex = wgpuDeviceCreateTexture(wgpu_mock_device(), &(WGPUTextureDescriptor){
        .size = {2, 2, 1}, .format = WGPUTextureFormat_RGBA8Unorm, .dimension = WGPUTextureDimension_2D,
        .mipLevelCount = 1, .sampleCount = 1, .usage = WGPUTextureUsage_TextureBinding,
    });
    sg_image image = sg_make_image(&(sg_image_desc){.width = 2, .height = 2, .wgpu_texture = tex});
    sg_view retained = sg_make_view(&(sg_view_desc){.texture.image = image});
    wgpu_mock_clear_calls();
    sg_destroy_buffer(b); sg_destroy_image(image); sg_commit();
    CHECK(!sg_query_stats().wgpu_memory.retired_count && !sg_query_stats().wgpu_memory.owned_buffers_created);
    CHECK(!wgpu_mock_count_calls(WGPU_MOCK_FUNC_wgpuBufferDestroy) && !wgpu_mock_count_calls(WGPU_MOCK_FUNC_wgpuTextureDestroy));
    CHECK(wgpu_mock_ref_count(raw) == 1 && wgpu_mock_ref_count(tex) == 2);
    sg_destroy_view(retained);
    CHECK(wgpu_mock_ref_count(tex) == 1);
    wgpuBufferDestroy(raw); wgpuBufferRelease(raw); wgpuTextureDestroy(tex); wgpuTextureRelease(tex);
    shutdown();
}

static void images(void) {
    setup(0);
    sg_image_desc desc = {
        .type = SG_IMAGETYPE_ARRAY, .width = 4, .height = 4, .num_slices = 3, .num_mipmaps = 3,
        .usage = {.copy_src = true, .copy_dst = true},
    };
    sg_image src = sg_make_image(&desc), dst = sg_make_image(&desc);
    CHECK(sg_query_image_state(src) == SG_RESOURCESTATE_VALID && sg_query_image_state(dst) == SG_RESOURCESTATE_VALID);
    WGPUTexture native = (WGPUTexture)sg_wgpu_query_image_info(src).tex;
    sg_view view = sg_make_view(&(sg_view_desc){.texture.image = src});
    sg_copy_image_to_image(&(sg_copy_image_to_image_desc){.src.image = src, .dst.image = dst});
    sg_destroy_image(src);
    wgpu_mock_texture_info_t info;
    CHECK(wgpu_mock_texture_info(native, &info) && !info.destroyed);
    CHECK(sg_query_stats().wgpu_memory.retired_image_bytes == 252);
    CHECK(sg_query_stats().wgpu_memory.retired_unsubmitted_bytes == 252);
    wgpu_mock_clear_calls();
    sg_commit();
    CHECK(wgpu_mock_texture_info(native, &info) && info.destroyed);
    CHECK(wgpu_mock_ref_count(native) == 1); // view retains the destroyed wrapper
    CHECK(wgpu_mock_find_call(WGPU_MOCK_FUNC_wgpuQueueSubmit, 0)
        < wgpu_mock_find_call(WGPU_MOCK_FUNC_wgpuTextureDestroy, 0));
    sg_destroy_view(view); sg_destroy_image(dst); sg_commit();
    CHECK(sg_query_stats().wgpu_memory.owned_images_created == 2 && sg_query_stats().wgpu_memory.owned_images_destroyed == 2);
    wgpu_mock_fail_next(WGPU_MOCK_OBJ_TEXTURE, 1);
    sg_image failed = sg_make_image(&desc);
    CHECK(sg_query_image_state(failed) == SG_RESOURCESTATE_FAILED);
    sg_destroy_image(failed);
    CHECK(sg_query_stats().wgpu_memory.owned_images_created == 2);
    shutdown();
}

static void bounded(void) {
    setup(128);
    sg_buffer dst = buffer();
    sg_buffer pending[100];
    for (int i = 0; i < 100; i++) { pending[i] = buffer(); }
    copy(pending[0], dst);
    for (int i = 0; i < 100; i++) { sg_destroy_buffer(pending[i]); }
    CHECK(sg_query_stats().wgpu_memory.retired_count == 100);
    CHECK(sg_query_stats().wgpu_memory.retired_unsubmitted_bytes == 1600);
    wgpu_mock_clear_calls();
    sg_commit();
    CHECK(wgpu_mock_count_calls(WGPU_MOCK_FUNC_wgpuBufferDestroy) == 64);
    CHECK(sg_query_stats().wgpu_memory.retired_count == 36 && !sg_query_stats().wgpu_memory.retired_unsubmitted_bytes);
    sg_buffer next = buffer();
    sg_destroy_buffer(next);
    CHECK(wgpu_mock_count_calls(WGPU_MOCK_FUNC_wgpuBufferDestroy) == 65);
    wgpu_mock_clear_calls();
    sg_commit();
    CHECK(!wgpu_mock_count_calls(WGPU_MOCK_FUNC_wgpuQueueSubmit));
    CHECK(!sg_query_stats().wgpu_memory.retired_count);
    // Shutdown drops unsubmitted passes before destroying their resources.
    sg_begin_pass(&(sg_pass){.compute = true});
    sg_destroy_buffer(dst);
    CHECK(sg_query_stats().wgpu_memory.retired_count == 1);
    wgpu_mock_clear_calls();
    sg_shutdown();
    CHECK(!wgpu_mock_count_calls(WGPU_MOCK_FUNC_wgpuQueueSubmit));
    CHECK(wgpu_mock_find_call(WGPU_MOCK_FUNC_wgpuCommandEncoderRelease, 0)
        < wgpu_mock_find_call(WGPU_MOCK_FUNC_wgpuBufferDestroy, 0));
    CHECK(!wgpu_mock_live_objects_total() && !wgpu_mock_total_errors());
    wgpu_mock_shutdown();
}

int main(void) {
    lifetimes(); borrowed(); images(); bounded();
    pid_t child = fork();
    CHECK(child >= 0);
    if (!child) {
        setup(2);
        sg_buffer a = buffer(), b = buffer(), c = buffer();
        copy(a, b);
        sg_destroy_buffer(a); sg_destroy_buffer(b); sg_destroy_buffer(c);
        _exit(1);
    }
    int status;
    CHECK(waitpid(child, &status, 0) == child && WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
    puts("WebGPU retirement: submit ordering, slot reuse, retained views/caches, borrowed/failed/mapped/staging, bounded drain, shutdown and saturation: PASS");
}
