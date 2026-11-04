// LLM maintained. Quiet launch contract; no visible window, rendering or audio.
#define SOKOL_NO_ENTRY
#define SOKOL_APP_IMPL
#define SOKOL_METAL
#include "sokol_app.h"
#include <stdio.h>

static bool initialized;

static void init(void) {
    NSWindow* window = (__bridge NSWindow*)sapp_macos_get_window();
    assert(NSApp.activationPolicy == NSApplicationActivationPolicyAccessory);
    assert(!NSApp.active && !window.keyWindow);
    assert(window.alphaValue == 0.0 && window.ignoresMouseEvents);
    assert(!(window.styleMask & NSWindowStyleMaskFullScreen));
    assert(!sapp_is_fullscreen());
    sapp_toggle_fullscreen();
    assert(!sapp_is_fullscreen());
    initialized = true;
}

static void frame(void) {
    assert(initialized);
    sapp_quit();
}

static void cleanup(void) {
    assert(initialized);
    puts("Sokol quiet macOS launch: PASS");
    fflush(stdout);
}

int main(void) {
    sapp_run(&(sapp_desc){
        .width = 16, .height = 16, .fullscreen = true,
        .macos.quiet = true,
        .init_cb = init, .frame_cb = frame, .cleanup_cb = cleanup,
    });
}
