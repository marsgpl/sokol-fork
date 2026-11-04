// LLM maintained. Accounting against upstream native-call mocks and existing cache helpers.
#define main caches_main
#include "sulrim_wgpu_caches.c"
#undef main

static void native_counts(sg_frame_stats_wgpu stats) {
    sg_frame_stats_wgpu calls = {0};
    for (int i = 0; i < wgpu_mock_num_calls(); i++) {
        const wgpu_mock_call_t* c = wgpu_mock_call(i);
        switch (c->func) {
            case WGPU_MOCK_FUNC_wgpuQueueWriteBuffer: calls.transfers.num_write_buffer++; calls.transfers.size_write_buffer += c->args[2].u; break;
            case WGPU_MOCK_FUNC_wgpuQueueWriteTexture: calls.transfers.num_write_texture++; calls.transfers.size_write_texture += c->args[2].u; break;
            case WGPU_MOCK_FUNC_wgpuCommandEncoderCopyBufferToBuffer: calls.transfers.num_copy_buffer++; calls.transfers.size_copy_buffer += c->args[4].u; break;
            case WGPU_MOCK_FUNC_wgpuQueueSubmit: calls.transfers.num_queue_submit++; break;
            case WGPU_MOCK_FUNC_wgpuCommandEncoderBeginRenderPass: calls.num_begin_render_pass++; break;
            case WGPU_MOCK_FUNC_wgpuCommandEncoderBeginComputePass: calls.num_begin_compute_pass++; break;
            case WGPU_MOCK_FUNC_wgpuRenderPassEncoderSetPipeline:
            case WGPU_MOCK_FUNC_wgpuComputePassEncoderSetPipeline: calls.num_set_pipeline++; break;
            case WGPU_MOCK_FUNC_wgpuRenderPassEncoderSetBindGroup:
            case WGPU_MOCK_FUNC_wgpuComputePassEncoderSetBindGroup:
                if (c->args[0].u == 0) calls.uniforms.num_set_bindgroup++; else calls.bindings.num_set_bindgroup++; break;
            case WGPU_MOCK_FUNC_wgpuDeviceCreateBindGroup: calls.bindings.num_create_bindgroup++; break;
            case WGPU_MOCK_FUNC_wgpuBindGroupRelease: calls.bindings.num_discard_bindgroup++; break;
            default: break;
        }
    }
#define SAME(key) CHECK(stats.key == calls.key)
    SAME(transfers.num_write_buffer); SAME(transfers.size_write_buffer);
    SAME(transfers.num_write_texture); SAME(transfers.size_write_texture);
    SAME(transfers.num_copy_buffer); SAME(transfers.size_copy_buffer); SAME(transfers.num_queue_submit);
    SAME(num_begin_render_pass); SAME(num_begin_compute_pass); SAME(num_set_pipeline);
    SAME(uniforms.num_set_bindgroup); SAME(bindings.num_set_bindgroup);
    SAME(bindings.num_create_bindgroup); SAME(bindings.num_discard_bindgroup);
#undef SAME
}
static void transfers(void) {
    setup(false); wgpu_mock_clear_calls();
    uint8_t bytes[128] = {1,2,3,4,5,6,7};
    sg_buffer initial = sg_make_buffer(&(sg_buffer_desc){.usage.index_buffer = true, .data = {bytes,7}});
    sg_buffer mapped = sg_make_buffer(&(sg_buffer_desc){.size = 8, .usage = {.index_buffer = true, .write_unsealed = true, .copy_dst = true}});
    sg_buffer stage = sg_make_buffer(&(sg_buffer_desc){.size = sizeof bytes,
        .usage = {.staging_buffer = true, .copy_src = true, .write_transient = true}});
    sg_buffer transient = sg_make_buffer(&(sg_buffer_desc){.size = 8, .usage = {.index_buffer = true, .write_transient = true}});
    sg_buffer dst = sg_make_buffer(&(sg_buffer_desc){.size = 8, .usage = {.index_buffer = true, .copy_dst = true, .copy_src = true}});
    sg_write_buffer_unsealed(&(sg_write_buffer_desc){.dst.buffer = mapped, .src.data = {bytes,7}}); sg_seal_buffer(mapped);
    sg_write_buffer_transient(&(sg_write_buffer_desc){.dst.buffer = transient, .src.data = {bytes,7}});
    sg_write_buffer_transient(&(sg_write_buffer_desc){.dst.buffer = stage, .src.data = {bytes,7}});
    sg_copy_buffer_to_buffer(&(sg_copy_buffer_to_buffer_desc){.src.buffer = stage, .dst.buffer = dst, .size = 7});
    sg_copy_buffer_to_buffer(&(sg_copy_buffer_to_buffer_desc){.src.buffer = dst, .dst.buffer = mapped, .size = 7});
    CHECK(sg_query_buffer_state(initial) == SG_RESOURCESTATE_VALID);
    sg_image mip = sg_make_image(&(sg_image_desc){.width = 2, .height = 2, .num_mipmaps = 2, .data = {bytes,20}});
    sg_image image = sg_make_image(&(sg_image_desc){.width = 2, .height = 2, .usage.write_transient = true});
    sg_image copy = sg_make_image(&(sg_image_desc){.width = 2, .height = 2, .usage.copy_dst = true});
    sg_write_image_transient(&(sg_write_image_desc){.dst.image = image,
        .src = {.data = SG_RANGE(bytes), .offset = 4, .bytes_per_row = 16, .bytes_per_slice = 32}, .size = {1,1,1}});
    sg_copy_buffer_to_image(&(sg_copy_buffer_to_image_desc){.src = {.buffer = stage, .bytes_per_row = 8, .bytes_per_slice = 16},
        .dst.image = copy, .size = {2,2,1}});
    const sg_frame_stats_wgpu before = sg_query_stats().cur_frame.wgpu;
    CHECK(before.transfers.size_memcpy == 27 && before.transfers.size_write_padding == 2);
    CHECK(before.transfers.size_copy_buffer == 8 && before.transfers.size_write_texture == 296);
    native_counts(before); sg_commit(); native_counts(sg_query_stats().prev_frame.wgpu);
    CHECK(sg_query_stats().prev_frame.size_copy_buffer_to_buffer == 14);
    wgpu_mock_clear_calls(); sg_commit(); native_counts(sg_query_stats().prev_frame.wgpu);
    sg_destroy_buffer(initial); sg_destroy_buffer(mapped); sg_destroy_buffer(stage); sg_destroy_buffer(transient); sg_destroy_buffer(dst);
    sg_destroy_image(mip); sg_destroy_image(image); sg_destroy_image(copy); shutdown();
}
static void padding(void) {
    setup(false); sg_buffer buffer = sg_make_buffer(&(sg_buffer_desc){.size = 32,
        .usage = {.index_buffer = true, .write_transient = true}});
    uint8_t bytes[17]; for (int i = 0; i < 17; i++) bytes[i] = (uint8_t)(i+1);
    sg_commit();
    for (size_t size = 1; size <= sizeof bytes; size++) {
        wgpu_mock_clear_calls();
        sg_write_buffer_transient(&(sg_write_buffer_desc){.dst.buffer = buffer, .src.data = {bytes,size}});
        sg_frame_stats_wgpu s = sg_query_stats().cur_frame.wgpu;
        CHECK(s.transfers.num_write_buffer == 1+(size%4 ? 1u : 0u));
        CHECK(s.transfers.size_write_buffer == (size+3)/4*4 && s.transfers.size_write_padding == (4-size%4)%4);
        CHECK(s.transfers.size_memcpy == size%4);
        const uint8_t* actual = wgpu_mock_buffer_data(sg_wgpu_query_buffer_info(buffer).buf);
        CHECK(!memcmp(actual,bytes,size));
        for (size_t i = size; i < (size+3)/4*4; i++) CHECK(!actual[i]);
        sg_commit(); native_counts(sg_query_stats().prev_frame.wgpu);
    }
    sg_destroy_buffer(buffer); shutdown();
}
static void profile_capacities(void) {
    setup(false); sg_shader a,b; sg_pipeline pa = uniform_pipeline(0,16,&a), pb = uniform_pipeline(1,16,&b);
    uint32_t x[4] = {1,2,3,4}, y[4] = {5,6,7,8};
    uint64_t id = sg_alloc_uniform_data_id();
    const uint32_t alignment = _sg.wgpu.limits.minUniformBufferOffsetAlignment;
    sg_wgpu_memory_stats memory = sg_query_stats().wgpu_memory;
    CHECK(memory.uniform_gpu_bytes == (uint64_t)sg_query_desc().uniform_buffer_size+65536);
    CHECK(memory.uniform_staging_bytes == memory.uniform_gpu_bytes && !memory.uniform_profiling_bytes);
    CHECK(memory.bindgroups_capacity == 8 && !memory.bindgroups_alive);
    CHECK(memory.bindgroup_cache_bytes == 8*sizeof(*_sg.wgpu.bindgroups_cache.items)
        + 9*(sizeof(_sg_wgpu_bindgroup_t)+sizeof(uint32_t)) + 8*sizeof(int));
    CHECK(memory.retirement_queue_bytes > 0);
    sg_commit(); wgpu_mock_clear_calls();
    // Default off, then on, off again with the allocation retained, stats off, on again.
    for (int frame = 0; frame < 5; frame++) {
        if (frame) sg_wgpu_profile_uniforms(frame != 2);
        if (frame == 3) sg_disable_stats(); else sg_enable_stats();
        const bool profiled = frame == 1 || frame == 4;
        sg_begin_pass(&(sg_pass){.compute = true}); sg_apply_pipeline(pa);
        sg_apply_uniforms_cached(0, &SG_RANGE(x), id); sg_apply_uniforms_cached(0, &SG_RANGE(x), id);
        sg_apply_uniforms(0, &SG_RANGE(x)); sg_apply_uniforms(0, &SG_RANGE(y));
        if (profiled) _sg.wgpu.uniform.records[2].hash = _sg.wgpu.uniform.records[0].hash;
        sg_dispatch(1,1,1); sg_dispatch(1,1,1); sg_end_pass();
        sg_begin_pass(&(sg_pass){.compute = true}); sg_apply_pipeline(pb); sg_apply_uniforms(1, &SG_RANGE(x)); sg_dispatch(1,1,1); sg_end_pass();
        uint8_t saved[4*256]; CHECK(alignment == 256); memcpy(saved,_sg.wgpu.uniform.staging,sizeof saved);
        CHECK(_sg.wgpu.uniform.num_records == (profiled ? 4u : 0u));
        sg_commit(); CHECK(!memcmp(saved,_sg.wgpu.uniform.staging,sizeof saved) && !_sg.wgpu.uniform.num_records);
        sg_frame_stats stats = sg_query_stats().prev_frame;
        CHECK(stats.wgpu.uniforms.size_hash == (profiled ? 64u : 0u));
        CHECK(stats.wgpu.uniforms.num_unique == (profiled ? 3u : 0u) && stats.wgpu.uniforms.size_unique == (profiled ? 48u : 0u));
        CHECK(profiled ? stats.wgpu.uniforms.size_compare > 0 : stats.wgpu.uniforms.size_compare == 0);
        if (frame == 3) CHECK(!stats.wgpu.uniforms.size_copy && !stats.wgpu.transfers.num_write_buffer);
        else {
            native_counts(stats.wgpu);
            CHECK(stats.num_apply_uniforms == 5 && stats.num_reuse_uniforms == 1);
            CHECK(stats.wgpu.uniforms.num_set_bindgroup == 2 && stats.wgpu.uniforms.size_copy == 64);
            CHECK(stats.wgpu.uniforms.size_write_buffer == 4*alignment && stats.wgpu.transfers.size_write_padding == 4*alignment-64);
        }
        memory = sg_query_stats().wgpu_memory;
        CHECK(memory.uniform_profiling_bytes == (frame ? (memory.uniform_gpu_bytes-1)/alignment+1 : 0)*sizeof(_sg_wgpu_uniform_record_t));
        wgpu_mock_clear_calls();
    }
    // Exact equality remains scoped to size, slot and shader uninit lifetime.
    sg_begin_pass(&(sg_pass){.compute = true}); sg_apply_pipeline(pa);
    _sg_wgpu_apply_uniforms(0,&SG_RANGE(x)); _sg_wgpu_apply_uniforms(0,&(sg_range){x,8});
    _sg_shader_t* shader = _sg_lookup_shader(a.id); ++shader->slot.uninit_count;
    _sg_lookup_pipeline(pa.id)->cmn.shader = _sg_shader_ref(shader);
    _sg_wgpu_apply_uniforms(0,&SG_RANGE(x)); sg_end_pass(); sg_commit();
    CHECK(sg_query_stats().prev_frame.wgpu.uniforms.num_unique == 3 && sg_query_stats().prev_frame.wgpu.uniforms.size_unique == 40);
    sg_destroy_pipeline(pa); sg_destroy_pipeline(pb); sg_destroy_shader(a); sg_destroy_shader(b); shutdown();
}
static void groups(void) {
    setup(false); wgpu_mock_clear_calls();
    sg_shader shader; sg_pipeline pip = uniform_pipeline(0,16,&shader);
    _sg_bindings_ptrs_t bindings = {.pip = _sg_lookup_pipeline(pip.id)};
    _sg_wgpu_bindgroup_t* groups[8];
    wgpu_mock_fail_next(WGPU_MOCK_OBJ_BIND_GROUP,1); expected_error = true;
    _sg_wgpu_bindgroup_t* failed = _sg_wgpu_create_bindgroup(&bindings); expected_error = false;
    CHECK(failed && failed->slot.state == SG_RESOURCESTATE_FAILED); _sg_wgpu_discard_bindgroup(failed);
    for (int i = 0; i < 8; i++) groups[i] = _sg_wgpu_create_bindgroup(&bindings);
    CHECK(sg_query_stats().wgpu_memory.bindgroups_alive == 8);
    uint32_t creates = sg_query_stats().cur_frame.wgpu.bindings.num_create_bindgroup;
    expected_error = true; CHECK(!_sg_wgpu_create_bindgroup(&bindings)); expected_error = false;
    CHECK(sg_query_stats().cur_frame.wgpu.bindings.num_create_bindgroup == creates);
    for (int i = 0; i < 8; i++) _sg_wgpu_discard_bindgroup(groups[i]);
    CHECK(!sg_query_stats().wgpu_memory.bindgroups_alive);
    sg_destroy_pipeline(pip); sg_destroy_shader(shader); native_counts(sg_query_stats().cur_frame.wgpu); shutdown();
}
static void external(void) {
    setup(false);
    const sg_frame_stats_transfers traffic = {1,2,3,4,UINT64_C(1)<<34,6,7,8,9};
    sg_add_transfer_stats(&traffic); sg_add_transfer_stats(&traffic); sg_commit();
    sg_frame_stats_transfers actual = sg_query_stats().prev_frame.external_transfers;
    CHECK(actual.num_write_buffer == 2 && actual.num_write_texture == 4 && actual.num_copy_buffer == 6 && actual.num_queue_submit == 8);
    CHECK(actual.size_write_buffer == UINT64_C(1)<<35 && actual.size_write_texture == 12 && actual.size_write_padding == 14 && actual.size_copy_buffer == 16 && actual.size_memcpy == 18);
    sg_disable_stats(); sg_add_transfer_stats(&traffic); sg_commit();
    sg_frame_stats_transfers zero = {0}; actual = sg_query_stats().prev_frame.external_transfers; CHECK(!memcmp(&actual,&zero,sizeof actual));
    shutdown();
}
int main(void) { padding(); transfers(); profile_capacities(); groups(); external(); puts("stats: native calls/bytes, padding, capacities, exact profiling and disabled frames PASS"); return 0; }
