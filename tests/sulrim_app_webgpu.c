// LLM maintained. Actual app device configuration, without game rendering or audio.
#include <webgpu/webgpu.h>
static void free_capabilities(WGPUSurfaceCapabilities caps);
#define wgpuSurfaceCapabilitiesFreeMembers free_capabilities
#define SOKOL_NO_ENTRY
#define SOKOL_APP_IMPL
#define SOKOL_WGPU
#include "sokol_app.h"
#include <emscripten/emscripten.h>
#undef wgpuSurfaceCapabilitiesFreeMembers

static bool ready;
static int capabilities_freed;

static void free_capabilities(WGPUSurfaceCapabilities caps) {
    assert(caps.formats && caps.formatCount);
    ++capabilities_freed;
    wgpuSurfaceCapabilitiesFreeMembers(caps);
}

static void init(void) {
    ready = true;
}

EMSCRIPTEN_KEEPALIVE void probe_start(int enabled) {
    sapp_run(&(sapp_desc){
        .width = 32, .height = 32, .high_dpi = true, .sample_count = 4,
        .html5.canvas_selector = "#canvas",
        .wgpu = {
            .prefer_low_power = enabled != 0,
            .enable_timestamp_query = enabled != 0,
            .enable_indirect_first_instance = enabled != 0,
        },
        .init_cb = init,
    });
}

EMSCRIPTEN_KEEPALIVE int probe_ready(void) {
    return ready;
}

EMSCRIPTEN_KEEPALIVE WGPUDevice probe_device(void) {
    assert(ready);
    return _sapp.wgpu.device;
}

EMSCRIPTEN_KEEPALIVE void probe_features(void) {
    assert(ready);
    assert(wgpuDeviceHasFeature(_sapp.wgpu.device, WGPUFeatureName_TimestampQuery) ==
        (_sapp.desc.wgpu.enable_timestamp_query &&
         wgpuAdapterHasFeature(_sapp.wgpu.adapter, WGPUFeatureName_TimestampQuery)));
    assert(wgpuDeviceHasFeature(_sapp.wgpu.device, WGPUFeatureName_IndirectFirstInstance) ==
        (_sapp.desc.wgpu.enable_indirect_first_instance &&
         wgpuAdapterHasFeature(_sapp.wgpu.adapter, WGPUFeatureName_IndirectFirstInstance)));
}

EMSCRIPTEN_KEEPALIVE WGPUTexture probe_texture(int depth) {
    return depth ? _sapp.wgpu.depth_stencil_tex : _sapp.wgpu.msaa_tex;
}

EMSCRIPTEN_KEEPALIVE int probe_capabilities_freed(void) {
    return capabilities_freed;
}

EMSCRIPTEN_KEEPALIVE void probe_recreate_surface(void) {
    _sapp_wgpu_discard_swapchain(false);
    _sapp_wgpu_create_swapchain(false);
}

EMSCRIPTEN_KEEPALIVE void probe_resize(void) {
    const EmscriptenUiEvent event = {0};
    _sapp_emsc_size_changed(0, &event, 0);
}
