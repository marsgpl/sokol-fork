// CPU-only experiment. No browser, GPU execution, queue writes or profiling overhead.
#define SLOPA_WGPU_STATS_NO_MAIN
#include "slopa_wgpu_stats.c"
#include <time.h>

static uint64_t now_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}

int main(void) {
    const size_t block_size = 2400; // Slopa game_objects_fs_uniforms, 2026-09-16.
    reset();
    sg_disable_stats();
    uint8_t* staging = malloc(4 * 1024 * 1024);
    assert(staging);
    _sg.wgpu.uniform.staging = staging;
    _sg.wgpu.uniform.num_bytes = 4 * 1024 * 1024;
    _sg_shader_t shader = {0};
    _sg_pipeline_t pipeline = {0};
    shader.slot.id = 1;
    shader.slot.state = SG_RESOURCESTATE_VALID;
    shader.cmn.uniform_blocks[1].stage = SG_SHADERSTAGE_FRAGMENT;
    shader.cmn.uniform_blocks[1].size = (uint32_t)block_size;
    pipeline.slot.id = 2;
    pipeline.slot.state = SG_RESOURCESTATE_VALID;
    pipeline.cmn.shader = _sg_shader_ref(&shader);
    _sg.cur_pip = _sg_pipeline_ref(&pipeline);
    _sg.cur_pass.in_pass = _sg.cur_pass.valid = true;
    _sg.next_draw_valid = true;
    uint8_t data[2400] = {1};
    uint64_t data_id = sg_alloc_uniform_data_id();
    const int counts[] = {16, 1024};
    for (int n = 0; n < 2; ++n) {
        for (int is_changing = 0; is_changing < 2; ++is_changing) {
            uint64_t elapsed[2] = {0};
            const int frames = 4000;
            for (int f = 0; f < frames + 100; ++f) {
                const int is_cached = f % 2;
                _sg.wgpu.uniform.offset = 0;
                ++_sg.uniform_cache_frame;
                const uint64_t start = now_ns();
                for (int i = 0; i < counts[n]; ++i) {
                    if (is_changing) { ++data[block_size - 1]; data_id = sg_alloc_uniform_data_id(); }
                    if (is_cached) sg_apply_uniforms_cached(1, &SG_RANGE(data), data_id);
                    else sg_apply_uniforms(1, &SG_RANGE(data));
                }
                if (f >= 100) elapsed[is_cached] += now_ns() - start;
            }
            const double applies = (frames / 2) * counts[n];
            printf("WebGPU CPU mock: bytes=%zu applies/commit=%d %s ordinary=%.1f cached=%.1f ns/apply\n",
                block_size, counts[n], is_changing ? "changing" : "identical", elapsed[0] / applies, elapsed[1] / applies);
        }
    }
    free(staging);
}
