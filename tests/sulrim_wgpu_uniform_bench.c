// LLM maintained. Legacy CPU-only identity benchmark on upstream mocks; no device performance claim.
#define main caches_main
#include "sulrim_wgpu_caches.c"
#undef main
#include <time.h>
static uint64_t now_ns(void) {
    struct timespec t; CHECK(!clock_gettime(CLOCK_MONOTONIC,&t));
    return (uint64_t)t.tv_sec*1000000000ull+(uint64_t)t.tv_nsec;
}
int main(void) {
    wgpu_mock_setup();
    sg_setup(&(sg_desc){.uniform_buffer_size = 4*1024*1024,
        .environment.wgpu.device = wgpu_mock_device(), .logger.func = logger});
    sg_disable_stats(); sg_shader shader; sg_pipeline pipeline = uniform_pipeline(1,2400,&shader);
    sg_begin_pass(&(sg_pass){.compute = true}); sg_apply_pipeline(pipeline);
    uint8_t data[2400] = {1}; uint64_t data_id = sg_alloc_uniform_data_id();
    const int counts[] = {16,1024};
    for (int n = 0; n < 2; n++) for (int changing = 0; changing < 2; changing++) {
        uint64_t elapsed[2] = {0};
        for (int frame = 0; frame < 4100; frame++) {
            const int cached = frame%2;
            _sg.wgpu.uniform.offset = 0; ++_sg.uniform_cache_frame;
            const uint64_t start = now_ns();
            for (int i = 0; i < counts[n]; i++) {
                if (changing) {++data[sizeof data-1]; data_id = sg_alloc_uniform_data_id();}
                if (cached) sg_apply_uniforms_cached(1,&SG_RANGE(data),data_id); else sg_apply_uniforms(1,&SG_RANGE(data));
            }
            if (frame >= 100) elapsed[cached] += now_ns()-start;
        }
        const double applies = 2000.0*counts[n];
        printf("CPU-only mock: bytes=2400 applies/commit=%d %s ordinary=%.1f cached=%.1f ns/apply\n",
            counts[n], changing ? "changing" : "identical", elapsed[0]/applies, elapsed[1]/applies);
    }
    CHECK(!_sg.wgpu.uniform.records && !_sg.stats.cur_frame.wgpu.uniforms.size_hash);
    sg_end_pass(); sg_destroy_pipeline(pipeline); sg_destroy_shader(shader); shutdown(); return 0;
}
