#ifndef m3_memory_h
#define m3_memory_h

#include "m3_core.h"
#include "wasm3.h"

typedef struct M3MemoryInfo
{
    u32     initPages;
    u32     maxPages;
    u32     pageSize;
}
M3MemoryInfo;


typedef struct M3Memory
{
    M3MemoryHeader *        mallocated;

    u32                     numPages;
    u32                     maxPages;
    u32                     pageSize;
}
M3Memory;

typedef M3Memory *          IM3Memory;

M3Result  ResizeMemory  (IM3Runtime io_runtime, u32 i_numPages);

#endif // m3_memory_h