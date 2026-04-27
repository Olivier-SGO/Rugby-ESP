#pragma once
#include <ArduinoJson.h>
#include <esp_heap_caps.h>

// ArduinoJson 7 allocator that forces all allocations into PSRAM.
// Usage: JsonDocument doc(&spiRamAlloc);
// This avoids SRAM realloc failures caused by heap_caps_malloc_extmem_enable
// when the JSON document grows beyond the largest contiguous SRAM block.
class SpiRamAllocator : public ArduinoJson::Allocator {
public:
    void* allocate(size_t size) override {
        return heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    }
    void deallocate(void* ptr) override {
        heap_caps_free(ptr);
    }
    void* reallocate(void* ptr, size_t new_size) override {
        return heap_caps_realloc(ptr, new_size, MALLOC_CAP_SPIRAM);
    }
};

inline SpiRamAllocator spiRamAlloc;
