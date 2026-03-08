#include "m3_core.h"
#include "m3_env.h"
#include "m3_exception.h"
#include "m3_memory.h"

#define MEM_PAGE_SIZE 65536

#ifndef SPARSE_PAGE_SIZE
#define SPARSE_PAGE_SIZE 256
#endif

void memEnsurePages(M3Memory* mem, const u32 offset, const u32 size) {
    const u32 requiredSize = (offset + size + mem->pageSize - 1) / mem->pageSize;
    if (requiredSize <= mem->numSparsePages) {
        return;
    }

    bytes_t* pages = m3_Realloc(
        "memory pages",
        mem->pages,
        sizeof(bytes_t) * requiredSize,
        sizeof(bytes_t) * mem->numSparsePages
    );
    for (u32 i = mem->numSparsePages; i < requiredSize; i++) {
        pages[i] = NULL;
    }
    mem->pages = pages;
}

static bool bytesIsEmpty(const u8* bytes, const u32 size) {
    for (u32 i = 0; i < size; i++) {
        if (bytes[i] != 0) {
            return false;
        }
    }
    return true;
}

static u32 min(const u32 a, const u32 b) {
    return a < b ? a : b;
}

static bool memHasPage(const M3Memory* mem, const u32 index) {
    return index < mem->numSparsePages && mem->pages[index] != NULL;
}

static void memDeletePageIfEmpty(M3Memory* mem, const u32 index) {
    if (!memHasPage(mem, index)) {
        return;
    }
    if (bytesIsEmpty(mem->pages[index], mem->pageSize)) {
        m3_Free(mem->pages[index]);
        mem->pages[index] = NULL;
        mem->pagesWithData--;
    }
}

static void memWriteToPage(M3Memory* mem, const u32 pageIdx, const u32 pageOff, const void* data, const u32 size) {
    if (!memHasPage(mem, pageIdx)) {
        mem->pages[pageIdx] = m3_Malloc("memory page", mem->pageSize);
        mem->pagesWithData++;
    }
    memcpy((void*)(mem->pages[pageIdx] + pageOff), data, size);
}

static void memLoadFromPage(const M3Memory* mem, const u32 pageIdx, const u32 pageOff, void* data, const u32 size) {
    if (pageIdx >= mem->numSparsePages || mem->pages[pageIdx] == NULL) {
        return;
    }
    memcpy(data, (void*)(mem->pages[pageIdx] + pageOff), size);
}

void memInit(M3Memory* mem) {
    mem->pageSize = SPARSE_PAGE_SIZE;
    mem->pagesWithData = 0;
    mem->numSparsePages = 0;
    mem->pages = NULL;
}

M3Result memStore(M3Memory* mem, const u32 offset, const void* data, const u32 size) {
    if (size == 0) return m3Err_none;
    if (offset + size > mem->header.length) {
        return m3Err_wasmMemoryOverflow;
    }

    memEnsurePages(mem, offset, size);

    const bool isEmpty = bytesIsEmpty(data, size);

    u32 pageIdx = offset / mem->pageSize;
    u32 pageOff = offset % mem->pageSize;
    u32 written = 0;
    while (written < size) {
        const u32 n = min(size-written, mem->pageSize-pageOff);

        if (!isEmpty || memHasPage(mem, pageIdx)) {
            memWriteToPage(mem, pageIdx, pageOff, data + written, n);
            if (isEmpty) {
                // current write is empty but page had data, check if we can delete it
                memDeletePageIfEmpty(mem, pageIdx);
            }
        }

        written += n;
        pageIdx++;
        pageOff = 0;
    }

    // if (mem->mergeThreshold > 0 && memShouldMergePages(mem)) {
    //     memMergePages(mem);
    // }

    return m3Err_none;
}

M3Result memLoad(const M3Memory* mem, const u32 offset, const u32 size, void* dest) {
    if (size == 0) return m3Err_none;
    if (offset + size > mem->header.length) {
        return m3Err_wasmMemoryOverflow;
    }

    memset(dest, 0, size);

    u32 pageIdx = offset / mem->pageSize;
    u32 pageOff = offset % mem->pageSize;
    u32 written = 0;
    while (written < size) {
        const u32 n = min(size-written, mem->pageSize-pageOff);
        memLoadFromPage(mem, pageIdx, pageOff, dest + written, n);
        written += n;
        pageIdx++;
        pageOff = 0;
    }

    return m3Err_none;
}

void memFree(M3Memory* mem) {
    if (mem == NULL) {
        return;
    }
    if (mem->pages == NULL) {
        return;
    }
    for (u32 i = 0; i < mem->numSparsePages; i++) {
        if (mem->pages[i] != NULL) {
            m3_Free(mem->pages[i]);
        }
    }
    m3_Free(mem->pages);
}

M3Result  ResizeMemory  (IM3Runtime io_runtime, u32 i_numPages)
{
    M3Memory* memory = & io_runtime->memory;
    const u32 numPagesToAlloc = i_numPages;

    if (numPagesToAlloc > memory->info.numPages) {
        return m3Err_wasmMemoryOverflow;
    }

    memory->header.length = i_numPages * MEM_PAGE_SIZE;
    memory->header.runtime = io_runtime;
    memory->header.maxStack = (m3slot_t *) io_runtime->stack + io_runtime->numStackSlots;

    return m3Err_none;
}