// LLM maintained. Runtime cap survives paused/recreated DisplayLinks; no window or GPU.
#define SOKOL_NO_ENTRY
#define SOKOL_APP_IMPL
#define SOKOL_METAL
#include "sokol_app.h"
#include <stdio.h>

int main(void) {
    _sapp.desc.swap_interval = 1;
    sapp_macos_set_frame_rate_cap(30);
    assert(_sapp_macos_mtl_preferred_fps(120) == 30);
    assert(_sapp_macos_mtl_preferred_fps(60) == 30);
    sapp_macos_set_frame_rate_cap(240);
    assert(_sapp_macos_mtl_preferred_fps(120) == 120);
    _sapp.desc.swap_interval = 2;
    assert(_sapp_macos_mtl_preferred_fps(120) == 60);
    sapp_macos_set_frame_rate_cap(0);
    assert(_sapp.mtl.fps_cap == 0);
    assert(_sapp_macos_mtl_preferred_fps(120) == 60);
    puts("Sokol runtime macOS frame-rate cap: PASS");
}
