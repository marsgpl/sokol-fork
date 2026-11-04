// LLM maintained. Native/Wasm initial payloads and persistent unsealed mapping.
#define SOKOL_IMPL
#define SOKOL_WGPU
#include "sokol_gfx.h"
#include <stdio.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "failed: %s:%d\n", #c, __LINE__); abort(); } } while (0)
static uint8_t bytes[256];
static WGPUBufferDescriptor created;
static size_t writes, mappings, unmaps, refs;
static size_t fail_write;
static bool is_create_failure;
static const WGPUBuffer buffer = (WGPUBuffer)1;

WGPUBuffer wgpuDeviceCreateBuffer(WGPUDevice device, const WGPUBufferDescriptor* desc) {
    (void)device;
    if (is_create_failure) { return 0; }
    created = *desc;
    memset(bytes, 0, sizeof bytes);
    return buffer;
}
void wgpuBufferAddRef(WGPUBuffer b) { CHECK(b == buffer); refs++; }
void* wgpuBufferGetMappedRange(WGPUBuffer b, size_t offset, size_t size) {
    CHECK(b == buffer && created.mappedAtCreation && offset == 0 && size == created.size);
    mappings++;
    return bytes;
}
WGPUStatus wgpuBufferWriteMappedRange(WGPUBuffer b, size_t offset, const void* data, size_t size) {
    CHECK(b == buffer && created.mappedAtCreation && offset % 8 == 0 && size % 4 == 0);
    CHECK(size && offset + size <= created.size);
    writes++;
    if (writes == fail_write) { return WGPUStatus_Error; }
    memcpy(bytes + offset, data, size);
    return WGPUStatus_Success;
}
void wgpuBufferUnmap(WGPUBuffer b) { CHECK(b == buffer); unmaps++; }

int main(void) {
        for (size_t size = 1; size <= 129; size++) {
            uint8_t* src = malloc(size);
            CHECK(src);
            for (size_t i = 0; i < size; i++) { src[i] = (uint8_t)(i * 17 + size); }
            for (int role = 0; role < 3; role++) {
                sg_buffer_desc desc = { .size = size, .data = { src, size }, .usage = { 0 } };
                desc.usage.vertex_buffer = role == 0;
                desc.usage.index_buffer = role == 1;
                desc.usage.storage_buffer = role == 2;
                _sg_buffer_t b = { 0 };
                _sg_buffer_common_init(&b.cmn, &desc);
                mappings = writes = unmaps = 0;
                CHECK(_sg_wgpu_create_buffer(&b, &desc) == SG_RESOURCESTATE_VALID);
                CHECK(created.mappedAtCreation && created.size == ((size + 3) & ~(size_t)3));
                CHECK(!(created.usage & WGPUBufferUsage_CopyDst));
                CHECK(memcmp(src, bytes, size) == 0 && unmaps == 1);
                for (size_t i = size; i < created.size; i++) { CHECK(bytes[i] == 0); }
                #if defined(__EMSCRIPTEN__)
                CHECK(mappings == 0 && writes == ((size > 7 && size % 4) ? 2 : 1));
                #else
                CHECK(mappings == 1 && writes == 0);
                #endif
            }
            free(src);
        }
    for (int mode = 0; mode < 3; mode++) {
        sg_buffer_desc desc = { .size = 16, .usage = { .storage_buffer = true } };
        desc.usage.copy_dst = true;
        desc.wgpu_buffer = mode == 2 ? buffer : 0;
        _sg_buffer_t b = { 0 };
        _sg_buffer_common_init(&b.cmn, &desc);
        writes = mappings = unmaps = 0;
        CHECK(_sg_wgpu_create_buffer(&b, &desc) == SG_RESOURCESTATE_VALID);
        CHECK(writes == 0 && mappings == 0 && unmaps == 0);
        if (mode != 2) { CHECK(!created.mappedAtCreation); }
    }
    CHECK(refs == 1);
    sg_buffer_desc desc = { .size = 11, .data = { bytes, 11 }, .usage = { .vertex_buffer = true } };
    _sg_buffer_t b = { 0 };
    _sg_buffer_common_init(&b.cmn, &desc);
    is_create_failure = true;
    CHECK(_sg_wgpu_create_buffer(&b, &desc) == SG_RESOURCESTATE_FAILED && !b.wgpu.buf);
    is_create_failure = false;
    #if defined(__EMSCRIPTEN__)
    created.mappedAtCreation = true;
    created.size = 12;
    for (fail_write = 1; fail_write <= 2; fail_write++) {
        writes = 0;
        CHECK(_sg_wgpu_write_initial_buffer(buffer, &desc.data) == WGPUStatus_Error);
        CHECK(writes == fail_write);
        #if defined(NDEBUG)
        writes = unmaps = 0;
        CHECK(_sg_wgpu_create_buffer(&b, &desc) == SG_RESOURCESTATE_FAILED);
        CHECK(unmaps == 1 && b.wgpu.buf && !b.wgpu.mapped_ptr);
        #endif
    }
    #endif
    fail_write = 0;
    desc = (sg_buffer_desc){ .size = 16, .usage = { .write_unsealed = true } };
    b = (_sg_buffer_t){0};
    _sg_buffer_common_init(&b.cmn, &desc);
    mappings = writes = unmaps = 0;
    CHECK(_sg_wgpu_create_buffer(&b, &desc) == SG_RESOURCESTATE_VALID);
    CHECK(created.mappedAtCreation && b.wgpu.mapped_ptr && mappings == 1 && writes == 0 && unmaps == 0);
    b.slot.state = SG_RESOURCESTATE_UNSEALED;
    uint8_t payload[5] = { 9, 7, 5, 3, 1 };
    _sg_wgpu_write_buffer_unsealed(&b, &(sg_write_buffer_desc){
        .src.data = {payload, sizeof payload}, .dst.offset = 3, .size = sizeof payload,
    });
    CHECK(memcmp(bytes + 3, payload, sizeof payload) == 0 && unmaps == 0);
    _sg_wgpu_seal_buffer(&b);
    CHECK(unmaps == 1 && !b.wgpu.mapped_ptr && writes == 0);
    puts("initial buffers: 387 exact payload/padding cases, persistent/injected and unsealed writes: PASS");
    return 0;
}
