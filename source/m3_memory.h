#ifndef m3_memory_h
#define m3_memory_h

typedef struct M3MemoryInfo
{
    u32     initPages;
    u32     maxPages;
    u32     numPages;
    u32     pageSize;
}
M3MemoryInfo;

typedef struct M3Memory
{
    M3MemoryHeader header;
    M3MemoryInfo info;

    u32      pageSize;
    u32      pagesWithData;
    u32      numSparsePages;
    bytes_t* pages;

    float mergeThreshold;
}
M3Memory;

typedef M3Memory* IM3Memory;

void memInit(M3Memory* mem);

void memMergePages(M3Memory* mem);

M3Result memStore(M3Memory* mem, const void* data, u32 offset, u32 size);

M3Result memLoad(const M3Memory* mem, void* dest, u32 offset, u32 size);

void memFree(M3Memory* mem);

M3Result  ResizeMemory  (IM3Runtime io_runtime, u32 i_numPages);

#endif // m3_memory_h