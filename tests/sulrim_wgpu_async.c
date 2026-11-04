// LLM maintained. Inject completion callbacks into upstream mocks; real compiles run separately.
#include "wgpu_mock.h"
#define SOKOL_IMPL
#define SOKOL_WGPU
#include "sokol_gfx.h"
#include <stdio.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); abort(); } } while (0)
typedef struct {
    sg_pipeline pip;
    WGPURenderPipeline render;
    WGPUComputePipeline compute;
    void* id;
    void* ticket;
} completion;
static sg_shader shader;
static sg_pipeline failed;
static unsigned errors;
static void logger(const char* tag, uint32_t level, uint32_t item, const char* message, uint32_t line, const char* file, void* data) {
    (void)tag; (void)line; (void)file; (void)data;
    CHECK(level == 1 && failed.id && sg_query_pipeline_state(failed) == SG_RESOURCESTATE_FAILED);
    CHECK(item == SG_LOGITEM_WGPU_CREATE_RENDER_PIPELINE_FAILED || item == SG_LOGITEM_WGPU_CREATE_COMPUTE_PIPELINE_FAILED);
    CHECK(message && !strcmp(message, "complete compiler message"));
    errors++;
}
static void setup(bool compute) {
    sg_setup(&(sg_desc){.environment.wgpu.device = wgpu_mock_device(), .environment.defaults = {
        .color_format = SG_PIXELFORMAT_RGBA8, .depth_format = SG_PIXELFORMAT_NONE, .sample_count = 1}, .logger.func = logger});
    sg_shader_desc desc = {0};
    if (compute) desc.compute_func.source = "@compute @workgroup_size(1) fn main() {}";
    else {
        desc.vertex_func.source = "@vertex fn main()->@builtin(position) vec4f {return vec4f(0.);}";
        desc.fragment_func.source = "@fragment fn main()->@location(0) vec4f {return vec4f(1.);}";
    }
    shader = sg_make_shader(&desc);
    CHECK(sg_query_shader_state(shader) == SG_RESOURCESTATE_VALID);
}
static completion pending(bool compute, sg_pipeline reuse) {
    sg_pipeline_desc desc = {.shader = shader, .compute = compute};
    sg_pipeline pip = reuse;
    if (pip.id) sg_init_pipeline(pip, &desc);
    else pip = sg_make_pipeline(&desc);
    CHECK(sg_query_pipeline_state(pip) == SG_RESOURCESTATE_VALID);
    _sg_pipeline_t* slot = _sg_lookup_pipeline(pip.id);
    completion c = {.pip = pip, .render = slot->wgpu.rpip, .compute = slot->wgpu.cpip};
    slot->wgpu.rpip = 0; slot->wgpu.cpip = 0; slot->slot.state = SG_RESOURCESTATE_PENDING;
    _sg_wgpu_async_new(slot, &c.id, &c.ticket);
    return c;
}
static void done(completion c) {
    if (c.compute) _sg_wgpu_async_compute_done(WGPUCreatePipelineAsyncStatus_Success, c.compute, (WGPUStringView){0}, c.id, c.ticket);
    else _sg_wgpu_async_render_done(WGPUCreatePipelineAsyncStatus_Success, c.render, (WGPUStringView){0}, c.id, c.ticket);
}
static int refs(completion c) { return wgpu_mock_ref_count(c.compute ? (void*)c.compute : (void*)c.render); }
static void lifetimes(bool compute) {
    wgpu_mock_setup(); setup(compute);
    completion first = pending(compute, (sg_pipeline){0});
    done(first);
    CHECK(sg_query_pipeline_state(first.pip) == SG_RESOURCESTATE_PENDING);
    sg_commit(); CHECK(sg_query_pipeline_state(first.pip) == SG_RESOURCESTATE_VALID && refs(first) == 1);
    sg_destroy_pipeline(first.pip); CHECK(!refs(first));

    completion old = pending(compute, (sg_pipeline){0});
    sg_uninit_pipeline(old.pip);
    CHECK(sg_query_pipeline_state(old.pip) == SG_RESOURCESTATE_ALLOC);
    completion current = pending(compute, old.pip);
    done(old); sg_commit();
    CHECK(!refs(old) && sg_query_pipeline_state(current.pip) == SG_RESOURCESTATE_PENDING);
    done(current); sg_commit(); CHECK(sg_query_pipeline_state(current.pip) == SG_RESOURCESTATE_VALID);
    sg_destroy_pipeline(current.pip);

    old = pending(compute, (sg_pipeline){0}); sg_destroy_pipeline(old.pip);
    current = pending(compute, (sg_pipeline){0});
    CHECK(_sg_slot_index(old.pip.id) == _sg_slot_index(current.pip.id) && old.pip.id != current.pip.id);
    done(old); sg_commit(); CHECK(!refs(old) && sg_query_pipeline_state(current.pip) == SG_RESOURCESTATE_PENDING);
    done(current); sg_commit(); sg_destroy_pipeline(current.pip);

    completion error = pending(compute, (sg_pipeline){0}); failed = error.pip;
    char text[] = "complete compiler message trailing garbage";
    const WGPUStringView message = {text, strlen("complete compiler message")};
    if (compute) {
        wgpuComputePipelineRelease(error.compute);
        _sg_wgpu_async_compute_done(WGPUCreatePipelineAsyncStatus_ValidationError, 0, message, error.id, error.ticket);
    } else {
        wgpuRenderPipelineRelease(error.render);
        _sg_wgpu_async_render_done(WGPUCreatePipelineAsyncStatus_ValidationError, 0, message, error.id, error.ticket);
    }
    memset(text, 'x', sizeof(text));
    CHECK(sg_query_pipeline_state(failed) == SG_RESOURCESTATE_PENDING);
    sg_commit(); CHECK(sg_query_pipeline_state(failed) == SG_RESOURCESTATE_FAILED);
    sg_destroy_pipeline(failed); failed.id = 0;

    sg_shutdown(); setup(compute);
    old = pending(compute, (sg_pipeline){0});
    completion ready = pending(compute, (sg_pipeline){0}); done(ready);
    sg_shutdown(); CHECK(!refs(ready) && refs(old) == 1);
    setup(compute); current = pending(compute, (sg_pipeline){0});
    CHECK(old.pip.id == current.pip.id && old.ticket != current.ticket);
    done(old); sg_commit(); CHECK(!refs(old) && sg_query_pipeline_state(current.pip) == SG_RESOURCESTATE_PENDING);
    done(current); sg_commit(); CHECK(sg_query_pipeline_state(current.pip) == SG_RESOURCESTATE_VALID);
    sg_shutdown();
    for (int i = 0; i < wgpu_mock_num_errors(); i++) fprintf(stderr, "%s\n", wgpu_mock_error(i));
    CHECK(!wgpu_mock_total_errors() && !wgpu_mock_call_log_overflow() && !wgpu_mock_live_objects_total());
    wgpu_mock_shutdown();
}
int main(void) {
    lifetimes(false); lifetimes(true); CHECK(errors == 2);
    puts("WebGPU async: commit publication, generation/uninit/session isolation, late-result ownership and complete fatal messages: PASS");
}
