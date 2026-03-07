#include "m3_env.h"
#include "m3_exception.h"

M3Result  ResizeMemory  (IM3Runtime io_runtime, u32 i_numPages)
{
    M3Result result = m3Err_none;

    u32 numPagesToAlloc = i_numPages;

    M3Memory * memory = & io_runtime->memory;

#if 0 // Temporary fix for memory allocation
    if (memory->mallocated) {
        memory->numPages = i_numPages;
        memory->mallocated->end = memory->wasmPages + (memory->numPages * io_runtime->memory.pageSize);
        return result;
    }

    i_numPagesToAlloc = 256;
#endif

    if (numPagesToAlloc <= memory->maxPages)
    {
        size_t numPageBytes = numPagesToAlloc * io_runtime->memory.pageSize;

#if d_m3MaxLinearMemoryPages > 0
        _throwif("linear memory limitation exceeded", numPagesToAlloc > d_m3MaxLinearMemoryPages);
#endif

        // Limit the amount of memory that gets actually allocated
        if (io_runtime->memoryLimit) {
            numPageBytes = M3_MIN (numPageBytes, io_runtime->memoryLimit);
        }

        size_t numBytes = numPageBytes + sizeof (M3MemoryHeader);

        size_t numPreviousBytes = memory->numPages * io_runtime->memory.pageSize;
        if (numPreviousBytes)
            numPreviousBytes += sizeof (M3MemoryHeader);

        void* newMem = m3_Realloc ("Wasm Linear Memory", memory->mallocated, numBytes, numPreviousBytes);
        _throwifnull(newMem);

        memory->mallocated = (M3MemoryHeader*)newMem;

# if d_m3LogRuntime
        M3MemoryHeader * oldMallocated = memory->mallocated;
# endif

        memory->numPages = numPagesToAlloc;

        memory->mallocated->length =  numPageBytes;
        memory->mallocated->runtime = io_runtime;

        memory->mallocated->maxStack = (m3slot_t *) io_runtime->stack + io_runtime->numStackSlots;

        m3log (runtime, "resized old: %p; mem: %p; length: %zu; pages: %d", oldMallocated, memory->mallocated, memory->mallocated->length, memory->numPages);
    }
    else result = m3Err_wasmMemoryOverflow;

    _catch: return result;
}
