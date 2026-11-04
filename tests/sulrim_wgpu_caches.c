// LLM maintained. Upstream mock contracts; real GPU bytes are checked separately.
#include "wgpu_mock.h"
#define SOKOL_IMPL
#define SOKOL_WGPU
#define SOKOL_VALIDATE_NON_FATAL
#define SOKOL_TRACE_HOOKS
#include "sokol_gfx.h"
#include <stdio.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); abort(); } } while (0)
static unsigned errors, last_error, traces;
static bool expected_error;
static void logger(const char* tag, uint32_t level, uint32_t item, const char* message, uint32_t line, const char* file, void* data) {
    (void)tag; (void)line; (void)file; (void)data;
    if (level <= 1) { if (!expected_error) {fprintf(stderr, "%s\n", message ? message : "error"); abort();} errors++; last_error=item; }
}
static void trace_uniform(int slot, const sg_range* data, void* user) { (void)slot; (void)data; (void)user; traces++; }
static void setup(bool disabled) {
    wgpu_mock_setup();
    sg_setup(&(sg_desc){.wgpu = {.bindgroups_cache_size = 8, .disable_bindgroups_cache = disabled},
        .environment.wgpu.device = wgpu_mock_device(), .environment.defaults = {
            .color_format = SG_PIXELFORMAT_RGBA8, .depth_format = SG_PIXELFORMAT_NONE, .sample_count = 1}, .logger.func = logger});
    CHECK(sg_isvalid()); sg_install_trace_hooks(&(sg_trace_hooks){.apply_uniforms = trace_uniform});
}
static void shutdown(void) {
    sg_shutdown();
    for (int i = 0; i < wgpu_mock_num_calls(); i++) {
        const wgpu_mock_call_t* call = wgpu_mock_call(i);
        if (call->func == WGPU_MOCK_FUNC_wgpuRenderPassEncoderSetBindGroup || call->func == WGPU_MOCK_FUNC_wgpuComputePassEncoderSetBindGroup) CHECK(call->args[1].p);
    }
    for (int i = 0; i < wgpu_mock_num_errors(); i++) fprintf(stderr, "%s\n", wgpu_mock_error(i));
    CHECK(!wgpu_mock_total_errors() && !wgpu_mock_call_log_overflow() && !wgpu_mock_live_objects_total());
    wgpu_mock_shutdown();
}
static sg_pipeline uniform_pipeline(int slot, uint32_t size, sg_shader* shader) {
    sg_shader_desc desc = {.compute_func.source = "@compute @workgroup_size(1) fn main() {}"};
    desc.uniform_blocks[slot] = (sg_shader_uniform_block){.stage = SG_SHADERSTAGE_COMPUTE, .size = size, .wgsl_group0_binding_n = slot+1};
    *shader = sg_make_shader(&desc); CHECK(sg_query_shader_state(*shader) == SG_RESOURCESTATE_VALID);
    wgpu_mock_bind_group_layout_info_t layout; wgpu_mock_bind_group_info_t group;
    CHECK(wgpu_mock_bind_group_layout_info(_sg_lookup_shader(shader->id)->wgpu.bgl_ub, &layout));
    CHECK(layout.num_entries == 1 && layout.entries[0].binding == (uint32_t)slot+1 && layout.entries[0].buffer.minBindingSize == size);
    CHECK(wgpu_mock_bind_group_info(_sg_lookup_shader(shader->id)->wgpu.bg_ub, &group));
    CHECK(group.num_entries == 1 && group.entries[0].size == size);
    sg_pipeline pip = sg_make_pipeline(&(sg_pipeline_desc){.compute = true, .shader = *shader});
    CHECK(sg_query_pipeline_state(pip) == SG_RESOURCESTATE_VALID); return pip;
}
static sg_pipeline listener_pipeline;
static uint64_t listener_id;
static bool listener_enabled;
static uint8_t payload[32];
static void listener(void* user) {
    (void)user;
    if (!listener_enabled) return;
    CHECK(!_sg.wgpu.uniform.offset);
    sg_begin_pass(&(sg_pass){.compute = true}); sg_apply_pipeline(listener_pipeline);
    sg_apply_uniforms_cached(0, &(sg_range){payload,16}, listener_id);
    CHECK(_sg.wgpu.uniform.offset == _sg.wgpu.limits.minUniformBufferOffsetAlignment);
    sg_dispatch(1,1,1); sg_end_pass(); listener_enabled = false;
}
static void uniforms(void) {
    setup(false); sg_shader a,b,c;
    sg_pipeline pa = uniform_pipeline(3,16,&a), pb = uniform_pipeline(0,16,&b), pc = uniform_pipeline(0,32,&c);
    sg_shader dual = sg_make_shader(&(sg_shader_desc){.compute_func.source = "@compute @workgroup_size(1) fn main() {}",
        .uniform_blocks[0] = {.stage = SG_SHADERSTAGE_COMPUTE, .size = 16, .wgsl_group0_binding_n = 9},
        .uniform_blocks[3] = {.stage = SG_SHADERSTAGE_COMPUTE, .size = 16, .wgsl_group0_binding_n = 2}});
    sg_pipeline pd = sg_make_pipeline(&(sg_pipeline_desc){.compute = true, .shader = dual});
    CHECK(sg_query_pipeline_state(pd) == SG_RESOURCESTATE_VALID);
    uint64_t id = sg_alloc_uniform_data_id(), other = sg_alloc_uniform_data_id();
    uint32_t align = _sg.wgpu.limits.minUniformBufferOffsetAlignment;
    sg_begin_pass(&(sg_pass){.compute = true}); sg_apply_pipeline(pa);
    sg_apply_uniforms_cached(3, &(sg_range){payload,16}, id);
    sg_apply_uniforms_cached(3, &(sg_range){payload,16}, other);
    sg_apply_uniforms_cached(3, &(sg_range){payload,16}, id);
    CHECK(_sg.wgpu.uniform.bind_offsets[3] == 0 && _sg.wgpu.uniform.offset == 2*align);
    sg_apply_pipeline(pb); sg_apply_uniforms_cached(0, &(sg_range){payload,16}, id); sg_dispatch(1,1,1);
    const wgpu_mock_compute_pass_encoder_state_t* state = wgpu_mock_compute_pass_encoder_state();
    CHECK(state->bind_groups[0].num_dynamic_offsets == 1 && state->bind_groups[0].dynamic_offsets[0] == 0);
    // A direct-map collision costs a copy and never reuses another ID's bytes.
    uint64_t collision = id;
    while (collision < id+_SG_UNIFORM_CACHE_SIZE) collision = sg_alloc_uniform_data_id();
    sg_apply_uniforms_cached(0, &(sg_range){payload,16}, collision);
    sg_apply_uniforms_cached(0, &(sg_range){payload,16}, id); CHECK(_sg.wgpu.uniform.offset == 4*align);
    sg_apply_pipeline(pd); sg_apply_uniforms_cached(0, &(sg_range){payload,16}, id);
    sg_apply_uniforms_cached(3, &(sg_range){payload,16}, other); sg_dispatch(1,1,1);
    CHECK(state->bind_groups[0].num_dynamic_offsets == 2 && state->bind_groups[0].dynamic_offsets[0] == align && state->bind_groups[0].dynamic_offsets[1] == 3*align);
#if defined(SOKOL_DEBUG)
    sg_apply_pipeline(pc); expected_error = true;
    sg_apply_uniforms_cached(0, &SG_RANGE(payload), id); expected_error = false;
    CHECK(last_error == SG_LOGITEM_VALIDATE_AU_CACHED_SIZE && errors == 1 && _sg.wgpu.uniform.offset == 4*align);
#endif
    // Disabled/release validation must still refuse a cache hit with another size.
    _sg.desc.disable_validation = true; sg_apply_pipeline(pc);
    sg_apply_uniforms_cached(0, &SG_RANGE(payload), id); CHECK(_sg.wgpu.uniform.offset == 5*align);
    sg_apply_pipeline(pb); sg_apply_uniforms_cached(0, &(sg_range){payload,16}, id); CHECK(_sg.wgpu.uniform.offset == 6*align);
    _sg.desc.disable_validation = false; sg_dispatch(1,1,1); sg_end_pass();
    listener_pipeline=pb; listener_id=id; listener_enabled=true;
    CHECK(sg_add_commit_listener((sg_commit_listener){listener,0}));
    sg_commit(); CHECK(!listener_enabled); sg_commit();
    CHECK(sg_remove_commit_listener((sg_commit_listener){listener,0}));
    sg_destroy_pipeline(pa); sg_destroy_pipeline(pb); sg_destroy_pipeline(pc);
    sg_destroy_shader(a); sg_destroy_shader(b); sg_destroy_shader(c); sg_destroy_pipeline(pd); sg_destroy_shader(dual);
    shutdown(); setup(false); CHECK(sg_alloc_uniform_data_id() > collision); shutdown();
}
static void resources(void) {
    setup(false);
    sg_shader_desc desc = {.compute_func.source = "@compute @workgroup_size(1) fn main() {}",
        .views[2].storage_buffer = {.stage = SG_SHADERSTAGE_COMPUTE, .readonly = true, .wgsl_group1_binding_n = 7},
        .views[5].texture = {.stage = SG_SHADERSTAGE_COMPUTE, .image_type = SG_IMAGETYPE_2D, .sample_type = SG_IMAGESAMPLETYPE_FLOAT},
        .texture_sampler_pairs[0] = {.stage = SG_SHADERSTAGE_COMPUTE, .view_slot = 5, .sampler_slot = 1},
        .samplers[1] = {.stage = SG_SHADERSTAGE_COMPUTE, .sampler_type = SG_SAMPLERTYPE_NONFILTERING, .wgsl_group1_binding_n = 12}};
    sg_shader shader = sg_make_shader(&desc); CHECK(sg_query_shader_state(shader) == SG_RESOURCESTATE_VALID);
    sg_pipeline pip = sg_make_pipeline(&(sg_pipeline_desc){.compute = true, .shader = shader});
    sg_pipeline variant = sg_make_pipeline(&(sg_pipeline_desc){.compute = true, .shader = shader});
    sg_sampler sampler = sg_make_sampler(&(sg_sampler_desc){0});
    const uint32_t texels[4] = {0};
    sg_image image = sg_make_image(&(sg_image_desc){.width = 2, .height = 2, .data = SG_RANGE(texels)});
    sg_view texture = sg_make_view(&(sg_view_desc){.texture.image = image});
    sg_buffer buffer = sg_make_buffer(&(sg_buffer_desc){.size = 4096, .usage.storage_buffer = true});
    sg_view views[10]; uint32_t ids[10] = {0};
    for (int i = 0; i < 10; i++) {
        views[i] = sg_make_view(&(sg_view_desc){.storage_buffer = {.buffer = buffer, .offset = i*256}});
        CHECK(sg_query_view_state(views[i]) == SG_RESOURCESTATE_VALID);
    }
    sg_bindings bindings = {.samplers[1] = sampler, .views[5] = texture};
    sg_begin_pass(&(sg_pass){.compute = true}); sg_apply_pipeline(pip);
    for (int i = 0; i < 8; i++) {
        bindings.views[2] = views[i]; sg_apply_bindings(&bindings); CHECK(_sg.next_draw_valid);
        ids[i] = _sg.wgpu.bindings_cache.bg.id; CHECK(ids[i]);
    }
    _sg_wgpu_bindgroup_handle_t* ways = _sg_wgpu_bindgroups_cache_ways(0);
    bindings.views[2] = views[0]; sg_apply_bindings(&bindings); CHECK(ways[0].id == ids[0]);
    bindings.views[2] = views[8]; sg_apply_bindings(&bindings); ids[8] = _sg.wgpu.bindings_cache.bg.id;
    CHECK(!_sg_wgpu_lookup_bindgroup(ids[1]) || _sg_wgpu_lookup_bindgroup(ids[1])->slot.state != SG_RESOURCESTATE_VALID);
    CHECK(ways[0].id == ids[8] && ways[1].id == ids[0]);
    const uint32_t creates = _sg.stats.cur_frame.wgpu.bindings.num_create_bindgroup;
    sg_apply_pipeline(variant); sg_apply_bindings(&bindings); CHECK(_sg.stats.cur_frame.wgpu.bindings.num_create_bindgroup == creates);
    sg_destroy_pipeline(pip); CHECK(ways[0].id == ids[8]);
    // Both type bits and the complete slot lifetime participate in invalidation.
    _sg_slot_t wrong = _sg_lookup_view(views[8].id)->slot; wrong.uninit_count++;
    _sg_wgpu_bindgroups_cache_invalidate(_SG_WGPU_BINDGROUPSCACHEITEMTYPE_VIEW, &wrong); CHECK(ways[0].id == ids[8]);
    wrong = _sg_lookup_view(views[8].id)->slot;
    _sg_wgpu_bindgroups_cache_invalidate(_SG_WGPU_BINDGROUPSCACHEITEMTYPE_SAMPLER, &wrong); CHECK(ways[0].id == ids[8]);
    // Full-set failure leaves a valid seven-way prefix and no failed cache entry.
    bindings.views[2] = views[9]; wgpu_mock_fail_next(WGPU_MOCK_OBJ_BIND_GROUP, 1); expected_error = true;
    sg_apply_bindings(&bindings); expected_error = false; CHECK(!_sg.next_draw_valid && !ways[7].id);
    for (int i = 0; i < 7; i++) CHECK(ways[i].id && _sg_wgpu_lookup_bindgroup(ways[i].id)->slot.state == SG_RESOURCESTATE_VALID);
    sg_apply_pipeline(variant); sg_apply_bindings(&bindings); CHECK(_sg.next_draw_valid && ways[7].id);
    // Parent destruction evicts every retained view, closes all gaps and resets redundancy state.
    sg_destroy_buffer(buffer); for (int i = 0; i < 8; i++) CHECK(!ways[i].id);
    CHECK(!_sg.wgpu.bindings_cache.bg.id);
    sg_end_pass(); sg_commit();
    sg_buffer replacement = sg_make_buffer(&(sg_buffer_desc){.size = 4096, .usage.storage_buffer = true});
    CHECK(_sg_slot_index(replacement.id) == _sg_slot_index(buffer.id) && replacement.id != buffer.id);
    sg_view fresh = sg_make_view(&(sg_view_desc){.storage_buffer.buffer = replacement});
    bindings.views[2] = fresh;
    sg_begin_pass(&(sg_pass){.compute = true}); sg_apply_pipeline(variant); sg_apply_bindings(&bindings); CHECK(ways[0].id);
    sg_uninit_sampler(sampler); for (int i = 0; i < 8; i++) CHECK(!ways[i].id);
    sg_init_sampler(sampler, &(sg_sampler_desc){0});
    sg_apply_pipeline(variant); sg_apply_bindings(&bindings); CHECK(ways[0].id);
    sg_uninit_view(fresh); for (int i = 0; i < 8; i++) CHECK(!ways[i].id);
    sg_init_view(fresh, &(sg_view_desc){.storage_buffer.buffer = replacement});
    sg_apply_pipeline(variant); sg_apply_bindings(&bindings); CHECK(ways[0].id);
    sg_destroy_shader(shader); for (int i = 0; i < 8; i++) CHECK(!ways[i].id);
    CHECK(!_sg.wgpu.bindings_cache.bg.id); sg_end_pass(); sg_commit();
    sg_destroy_view(fresh); sg_destroy_buffer(replacement);
    for (int i = 0; i < 10; i++) sg_destroy_view(views[i]);
    sg_destroy_view(texture); sg_destroy_image(image);
    sg_destroy_sampler(sampler); sg_destroy_pipeline(variant); shutdown();
}
static void empty_groups(bool disabled) {
    setup(disabled);
    sg_shader empty = sg_make_shader(&(sg_shader_desc){.compute_func.source = "@compute @workgroup_size(1) fn main() {}"});
    sg_shader resource = sg_make_shader(&(sg_shader_desc){.compute_func.source = "@compute @workgroup_size(1) fn main() {}",
        .views[2].storage_buffer = {.stage = SG_SHADERSTAGE_COMPUTE, .readonly = true, .wgsl_group1_binding_n = 7}});
    sg_pipeline ep = sg_make_pipeline(&(sg_pipeline_desc){.compute = true, .shader = empty});
    sg_pipeline rp = sg_make_pipeline(&(sg_pipeline_desc){.compute = true, .shader = resource});
    sg_buffer buffer = sg_make_buffer(&(sg_buffer_desc){.size = 16, .usage.storage_buffer = true});
    sg_view view = sg_make_view(&(sg_view_desc){.storage_buffer.buffer = buffer});
    for (int pass = 0; pass < 2; pass++) {
        sg_begin_pass(&(sg_pass){.compute = true});
        const wgpu_mock_compute_pass_encoder_state_t* state = wgpu_mock_compute_pass_encoder_state();
        CHECK(!state->bind_groups[0].num_set_calls && !state->bind_groups[1].num_set_calls);
        sg_apply_pipeline(rp); sg_apply_bindings(&(sg_bindings){.views[2] = view}); sg_dispatch(1,1,1);
        CHECK(state->bind_groups[0].group && state->bind_groups[1].group);
        const void* group = state->bind_groups[1].group;
        sg_apply_pipeline(ep); sg_dispatch(1,1,1);
        CHECK(state->bind_groups[1].group == _sg_lookup_shader(empty.id)->wgpu.bg_view_smp_empty && !_sg.wgpu.bindings_cache.bg.id);
        sg_apply_pipeline(rp); sg_apply_bindings(&(sg_bindings){.views[2] = view}); sg_dispatch(1,1,1);
        CHECK(state->bind_groups[0].group && state->bind_groups[1].group && state->bind_groups[1].num_set_calls == 3);
        if (!disabled) CHECK(state->bind_groups[1].group == group);
        sg_end_pass(); sg_commit();
    }
    sg_destroy_view(view); sg_destroy_buffer(buffer); sg_destroy_pipeline(ep); sg_destroy_pipeline(rp);
    sg_destroy_shader(empty); sg_destroy_shader(resource); shutdown();
}
static void render_state(void) {
    setup(false);
    sg_shader shader = sg_make_shader(&(sg_shader_desc){.vertex_func.source = "@vertex fn main()->@builtin(position) vec4f {return vec4f(0);}",
        .fragment_func.source = "@fragment fn main()->@location(0) vec4f {return vec4f(1);}"});
    sg_pipeline pipelines[4];
    for (int i = 0; i < 4; i++) {
        pipelines[i] = sg_make_pipeline(&(sg_pipeline_desc){.shader = shader,
            .blend_color = {.r = i >= 2 ? 1 : 0}, .stencil.ref = i == 3 ? 1 : 0});
        CHECK(sg_query_pipeline_state(pipelines[i]) == SG_RESOURCESTATE_VALID);
    }
    sg_image image = sg_make_image(&(sg_image_desc){.width = 1, .height = 1, .usage.color_attachment = true});
    sg_view color = sg_make_view(&(sg_view_desc){.color_attachment.image = image});
    wgpu_mock_clear_calls();
    sg_begin_pass(&(sg_pass){.attachments.colors[0] = color});
    for (int i = 0; i < 4; i++) sg_apply_pipeline(pipelines[i]);
    CHECK(wgpu_mock_count_calls(WGPU_MOCK_FUNC_wgpuRenderPassEncoderSetBlendConstant) == 2);
    CHECK(wgpu_mock_count_calls(WGPU_MOCK_FUNC_wgpuRenderPassEncoderSetStencilReference) == 2);
    const wgpu_mock_render_pass_encoder_state_t* state = wgpu_mock_render_pass_encoder_state();
    CHECK(state->blend_constant.r == 1 && state->stencil_reference == 1);
    sg_reset_state_cache(); sg_apply_pipeline(pipelines[3]);
    CHECK(wgpu_mock_count_calls(WGPU_MOCK_FUNC_wgpuRenderPassEncoderSetBlendConstant) == 3);
    CHECK(wgpu_mock_count_calls(WGPU_MOCK_FUNC_wgpuRenderPassEncoderSetStencilReference) == 3);
    sg_end_pass();
    sg_begin_pass(&(sg_pass){.attachments.colors[0] = color}); sg_apply_pipeline(pipelines[3]); sg_end_pass(); sg_commit();
    CHECK(wgpu_mock_count_calls(WGPU_MOCK_FUNC_wgpuRenderPassEncoderSetBlendConstant) == 4);
    CHECK(wgpu_mock_count_calls(WGPU_MOCK_FUNC_wgpuRenderPassEncoderSetStencilReference) == 4);
    CHECK(sg_query_stats().prev_frame.wgpu.num_set_blend_constant == 4 && sg_query_stats().prev_frame.wgpu.num_set_stencil_reference == 4);
    sg_destroy_view(color); sg_destroy_image(image);
    for (int i = 0; i < 4; i++) sg_destroy_pipeline(pipelines[i]);
    sg_destroy_shader(shader); shutdown();
}
int main(void) { uniforms(); resources(); empty_groups(false); empty_groups(true); render_state(); puts("cache mocks: uniform sizes/identity, LRU/lifetimes, empty groups and dynamic state pass"); }
