// Native-call mocks exercise accounting and timestamp placement, not GPU correctness.
#define SOKOL_IMPL
#define SOKOL_WGPU
#include "sokol_gfx.h"
#include <stdio.h>

static struct {
    uint32_t writes, binds, renders, computes, submits;
    uint64_t bytes;
    WGPUPassTimestampWrites timestamps[8];
    uint32_t timestamp_count;
} calls;

WGPUBool wgpuDeviceHasFeature(WGPUDevice device, WGPUFeatureName feature) {
    (void)device;
    return feature == WGPUFeatureName_TimestampQuery;
}
void wgpuQueueWriteBuffer(WGPUQueue queue, WGPUBuffer buffer, uint64_t offset, const void* data, size_t size) {
    (void)queue; (void)buffer; (void)offset; (void)data;
    calls.writes++;
    calls.bytes += size;
}
void wgpuComputePassEncoderSetBindGroup(WGPUComputePassEncoder pass, uint32_t index, WGPUBindGroup group, size_t count, const uint32_t* offsets) {
    (void)pass; (void)index; (void)count; (void)offsets;
    assert(group);
    calls.binds++;
}
void wgpuRenderPassEncoderSetBindGroup(WGPURenderPassEncoder pass, uint32_t index, WGPUBindGroup group, size_t count, const uint32_t* offsets) {
    (void)pass; (void)index; (void)count; (void)offsets;
    assert(group);
    calls.binds++;
}
void wgpuComputePassEncoderSetPipeline(WGPUComputePassEncoder pass, WGPUComputePipeline pipeline) { (void)pass; (void)pipeline; }
void wgpuRenderPassEncoderSetPipeline(WGPURenderPassEncoder pass, WGPURenderPipeline pipeline) { (void)pass; (void)pipeline; }
void wgpuRenderPassEncoderSetBlendConstant(WGPURenderPassEncoder pass, const WGPUColor* color) { (void)pass; (void)color; }
void wgpuRenderPassEncoderSetStencilReference(WGPURenderPassEncoder pass, uint32_t value) { (void)pass; (void)value; }
WGPUCommandEncoder wgpuDeviceCreateCommandEncoder(WGPUDevice device, const WGPUCommandEncoderDescriptor* desc) {
    (void)device; (void)desc;
    return (WGPUCommandEncoder)1;
}
WGPUComputePassEncoder wgpuCommandEncoderBeginComputePass(WGPUCommandEncoder encoder, const WGPUComputePassDescriptor* desc) {
    (void)encoder;
    calls.computes++;
    if (desc->timestampWrites) calls.timestamps[calls.timestamp_count++] = *desc->timestampWrites;
    return (WGPUComputePassEncoder)1;
}
WGPURenderPassEncoder wgpuCommandEncoderBeginRenderPass(WGPUCommandEncoder encoder, const WGPURenderPassDescriptor* desc) {
    (void)encoder;
    calls.renders++;
    if (desc->timestampWrites) calls.timestamps[calls.timestamp_count++] = *desc->timestampWrites;
    return (WGPURenderPassEncoder)1;
}
void wgpuComputePassEncoderEnd(WGPUComputePassEncoder pass) { (void)pass; }
void wgpuComputePassEncoderRelease(WGPUComputePassEncoder pass) { (void)pass; }
void wgpuRenderPassEncoderEnd(WGPURenderPassEncoder pass) { (void)pass; }
void wgpuRenderPassEncoderRelease(WGPURenderPassEncoder pass) { (void)pass; }
WGPUCommandBuffer wgpuCommandEncoderFinish(WGPUCommandEncoder encoder, const WGPUCommandBufferDescriptor* desc) {
    (void)encoder; (void)desc;
    return (WGPUCommandBuffer)1;
}
void wgpuCommandEncoderRelease(WGPUCommandEncoder encoder) { (void)encoder; }
void wgpuCommandBufferRelease(WGPUCommandBuffer buffer) { (void)buffer; }
void wgpuQueueSubmit(WGPUQueue queue, size_t count, const WGPUCommandBuffer* buffers) {
    (void)queue; (void)count; (void)buffers;
    calls.submits++;
}

// These tests never create or retire owned public resources.
void wgpuBufferDestroy(WGPUBuffer buffer) { (void)buffer; assert(false); }
void wgpuBufferRelease(WGPUBuffer buffer) { (void)buffer; assert(false); }
void wgpuTextureDestroy(WGPUTexture texture) { (void)texture; assert(false); }
void wgpuTextureRelease(WGPUTexture texture) { (void)texture; assert(false); }

static void reset(void) {
    static uint8_t staging[4096];
    if (_sg.wgpu.uniform.records) _sg_free(_sg.wgpu.uniform.records);
    memset(&_sg, 0, sizeof _sg);
    memset(&calls, 0, sizeof calls);
    _sg.valid = true;
    _sg.stats_enabled = true;
    _sg.wgpu.dev = (WGPUDevice)1;
    _sg.wgpu.queue = (WGPUQueue)1;
    _sg.wgpu.uniform.buf = (WGPUBuffer)1;
    _sg.wgpu.uniform.num_bytes = sizeof staging;
    _sg.wgpu.uniform.staging = staging;
    _sg.wgpu.limits.minUniformBufferOffsetAlignment = 256;
}

static void test_padding(void) {
    reset();
    uint8_t payload[7] = {0};
    _sg_buffer_t buffer = {0};
    buffer.cmn.size = 8;
    buffer.wgpu.buf = (WGPUBuffer)1;
    _sg_wgpu_copy_buffer_data(&buffer, 0, &(sg_range){payload, sizeof payload});
    assert(calls.writes == 2 && calls.bytes == 8);
    assert(_sg.stats.cur_frame.wgpu.transfers.num_write_buffer == calls.writes);
    assert(_sg.stats.cur_frame.wgpu.transfers.size_write_buffer == calls.bytes);
    assert(_sg.stats.cur_frame.wgpu.transfers.size_write_padding == 1);
    sg_disable_stats();
    _sg_wgpu_copy_buffer_data(&buffer, 0, &(sg_range){payload, sizeof payload});
    assert(calls.writes == 4);
    assert(_sg.stats.cur_frame.wgpu.transfers.num_write_buffer == 2);
}

static void test_uniform_and_empty_binds(void) {
    reset();
    _sg_shader_t shader = {0};
    _sg_pipeline_t pipeline = {0};
    shader.slot.id = 1;
    shader.wgpu.bg_ub = (WGPUBindGroup)1;
    shader.wgpu.bg_view_smp_empty = (WGPUBindGroup)2;
    pipeline.slot.id = 2;
    pipeline.cmn.shader = _sg_shader_ref(&shader);
    pipeline.wgpu.rpip = (WGPURenderPipeline)1;
    pipeline.wgpu.cpip = (WGPUComputePipeline)1;
    _sg.cur_pip = _sg_pipeline_ref(&pipeline);
    _sg.wgpu.rpass_enc = (WGPURenderPassEncoder)1;
    _sg_wgpu_apply_pipeline(&pipeline);
    assert(calls.binds == 1);
    assert(_sg.stats.cur_frame.wgpu.bindings.num_set_bindgroup == 1);
    float payload[4] = {0};
    _sg_wgpu_apply_uniforms(0, &(sg_range){payload, sizeof payload});
    _sg_wgpu_apply_uniforms(0, &(sg_range){payload, sizeof payload});
    assert(_sg.stats.cur_frame.wgpu.uniforms.num_set_bindgroup == 0);
    _sg_wgpu_uniform_system_set_bindgroup();
    assert(calls.binds == 2);
    assert(_sg.stats.cur_frame.wgpu.uniforms.num_set_bindgroup == 1);
    _sg_wgpu_uniform_system_on_commit();
    assert(calls.bytes == 512);
    assert(_sg.stats.cur_frame.wgpu.uniforms.size_copy == 32);
    assert(_sg.stats.cur_frame.wgpu.transfers.size_write_padding == 480);
    pipeline.cmn.is_compute = true;
    _sg.cur_pass.is_compute = true;
    _sg.wgpu.cpass_enc = (WGPUComputePassEncoder)1;
    _sg_wgpu_apply_pipeline(&pipeline);
    _sg_wgpu_uniform_system_set_bindgroup();
    assert(calls.binds == 4);
    assert(_sg.stats.cur_frame.wgpu.bindings.num_set_bindgroup == 2);
    assert(_sg.stats.cur_frame.wgpu.uniforms.num_set_bindgroup == 2);
}

static void test_real_pass_timestamps(void) {
    reset();
    _sg_attachments_ptrs_t attachments = {.empty = true};
    sg_pass compute = {.compute = true};
    sg_pass render = {0};
    render.swapchain.wgpu.render_view = (const void*)1;
    render.action.colors[0].load_action = SG_LOADACTION_CLEAR;
    render.action.colors[0].store_action = SG_STOREACTION_STORE;
    sg_wgpu_arm_frame_timestamps((const void*)1);
    _sg_wgpu_begin_pass(&compute, &attachments);
    _sg_wgpu_end_pass(&attachments);
    _sg_wgpu_begin_pass(&render, &attachments);
    _sg_wgpu_end_pass(&attachments);
    assert(calls.computes == 1 && calls.renders == 1 && calls.binds == 0);
    assert(_sg.stats.cur_frame.wgpu.bindings.num_set_bindgroup == 0);
    assert(calls.timestamp_count == 2);
    assert(calls.timestamps[0].beginningOfPassWriteIndex == 0);
    assert(calls.timestamps[0].endOfPassWriteIndex == 1);
    assert(calls.timestamps[1].beginningOfPassWriteIndex == WGPU_QUERY_SET_INDEX_UNDEFINED);
    assert(calls.timestamps[1].endOfPassWriteIndex == 1);
    assert(sg_wgpu_frame_timestamp_passes() == 2);
    _sg_wgpu_commit();
    assert(calls.submits == 1);
    assert(_sg.stats.cur_frame.wgpu.transfers.num_queue_submit == calls.submits);
    assert(sg_wgpu_frame_timestamp_passes() == 0);
    sg_wgpu_arm_frame_timestamps((const void*)1);
    _sg_wgpu_commit();
    assert(calls.submits == 1 && sg_wgpu_frame_timestamp_passes() == 0);
    _sg_wgpu_begin_pass(&compute, &attachments);
    _sg_wgpu_end_pass(&attachments);
    assert(calls.timestamp_count == 2);
}

static void test_external_frame_lifetime(void) {
    reset();
    sg_frame_stats_transfers transfer = {.num_write_buffer = 1, .size_write_buffer = 64};
    sg_add_transfer_stats(&transfer);
    _sg_update_stats();
    assert(_sg.stats.prev_frame.external_transfers.num_write_buffer == 1);
    assert(_sg.stats.prev_frame.external_transfers.size_write_buffer == 64);
    assert(_sg.stats.cur_frame.external_transfers.num_write_buffer == 0);
    sg_disable_stats();
    sg_add_transfer_stats(&transfer);
    assert(_sg.stats.cur_frame.external_transfers.num_write_buffer == 0);
}

static void test_unique_payloads_and_capacities(void) {
    reset();
    _sg_shader_t shader = {0};
    _sg_pipeline_t pipeline = {0};
    shader.slot.id = 1;
    pipeline.slot.id = 2;
    pipeline.cmn.shader = _sg_shader_ref(&shader);
    _sg.cur_pip = _sg_pipeline_ref(&pipeline);
    uint32_t a[4] = {1,2,3,4}, b[4] = {5,6,7,8};
    _sg_wgpu_apply_uniforms(0, &(sg_range){a, sizeof a});
    _sg_wgpu_apply_uniforms(0, &(sg_range){a, sizeof a});
    _sg_wgpu_apply_uniforms(0, &(sg_range){b, sizeof b});
    // Force a hash collision: equality must still compare the actual bytes.
    _sg.wgpu.uniform.records[2].hash = _sg.wgpu.uniform.records[0].hash;
    _sg_wgpu_apply_uniforms(1, &(sg_range){a, sizeof a});
    ++shader.slot.uninit_count;
    pipeline.cmn.shader = _sg_shader_ref(&shader);
    _sg_wgpu_apply_uniforms(0, &(sg_range){a, sizeof a});
    _sg_wgpu_apply_uniforms(0, &(sg_range){a, sizeof a / 2});
    ++pipeline.slot.id; // same shader/slot/data is reusable across pipelines
    _sg.cur_pip = _sg_pipeline_ref(&pipeline);
    _sg_wgpu_apply_uniforms(0, &(sg_range){a, sizeof a});
    uint8_t before[7 * 256];
    memcpy(before, _sg.wgpu.uniform.staging, sizeof before);
    _sg_wgpu_uniform_system_on_commit();
    assert(memcmp(before, _sg.wgpu.uniform.staging, sizeof before) == 0);
    assert(_sg.stats.cur_frame.wgpu.uniforms.num_unique == 5);
    assert(_sg.stats.cur_frame.wgpu.uniforms.size_unique == 72);
    assert(_sg.stats.cur_frame.wgpu.uniforms.size_copy == 104);
    assert(_sg.stats.cur_frame.wgpu.uniforms.size_hash == 104);
    assert(_sg.stats.cur_frame.wgpu.uniforms.size_compare > 0);
    assert(calls.bytes == 7 * 256 && _sg.wgpu.uniform.num_records == 0);
    _sg.wgpu.bindgroups_pool.pool.size = 9;
    _sg.wgpu.bindgroups_pool.pool.queue_top = 5;
    _sg.wgpu.bindgroups_cache.num = 8;
    sg_wgpu_memory_stats memory = sg_query_stats().wgpu_memory;
    assert(memory.uniform_gpu_bytes == 4096 && memory.uniform_staging_bytes == 4096);
    assert(memory.uniform_profiling_bytes == 16 * sizeof(_sg_wgpu_uniform_record_t));
    assert(memory.bindgroups_alive == 3 && memory.bindgroups_capacity == 8);
    assert(memory.bindgroup_cache_bytes == 8 * sizeof(_sg_wgpu_bindgroup_handle_t)
           + 9 * (sizeof(_sg_wgpu_bindgroup_t) + sizeof(uint32_t)) + 8 * sizeof(int));
    _sg_update_stats();
    _sg_wgpu_apply_uniforms(0, &(sg_range){a, sizeof a});
    _sg_wgpu_uniform_system_on_commit();
    assert(_sg.stats.cur_frame.wgpu.uniforms.size_unique == 16); // a fresh commit
    reset();
    sg_disable_stats();
    _sg_wgpu_apply_uniforms(0, &(sg_range){a, sizeof a});
    assert(!_sg.wgpu.uniform.records && _sg.stats.cur_frame.wgpu.uniforms.size_hash == 0);
}

int main(void) {
    test_padding();
    test_uniform_and_empty_binds();
    test_real_pass_timestamps();
    test_external_frame_lifetime();
    test_unique_payloads_and_capacities();
    puts("Sokol WebGPU instrumentation: 5 tests passed");
}
