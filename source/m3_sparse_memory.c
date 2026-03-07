#include "m3_core.h"

#define PAGE_SIZE 65536

typedef struct M3Memory {
    u32     numPages;
    u32     maxPages;

    u32 pageSize;
    u32 pagesWithData;
    u32 _numPages;
    bytes_t* pages;

    f32 mergeThreshold;
} M3Memory;


void memEnsurePages(M3Memory* mem, const u32 offset, const u32 size) {
    const u32 requiredSize = ((offset+size) / mem->pageSize) + 1;
    if (requiredSize <= mem->_numPages) {
        return;
    }

    bytes_t* pages = m3_Realloc("memory pages", mem->pages, sizeof(bytes_t) * requiredSize, sizeof(bytes_t) * mem->_numPages);
    for (u32 i = mem->_numPages; i < requiredSize; i++) {
        pages[i] = NULL;
    }
    mem->pages = pages;
}

bool bytesIsEmpty(const void * data, u32 size);

static u32 min(const u32 a, const u32 b) {
    return a < b ? a : b;
}

static bool memHasPage(const M3Memory* mem, const u32 index) {
    return index < mem->_numPages && mem->pages[index] != NULL;
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
    if (pageIdx >= mem->_numPages || mem->pages[pageIdx] == NULL) {
        return;
    }
    memcpy(data, (void*)(mem->pages[pageIdx] + pageOff), size);
}

static size_t calculateOverheadCost(const u32 numPages) {
    return sizeof(bytes_t) * numPages; // page pointer array
}

static f32 calculateOverhead(const u32 numPages, const u32 numPagesWithData, const u32 pageSize) {
    const size_t cost = calculateOverheadCost(numPages);
    const size_t size = (size_t)pageSize * numPagesWithData;
    return (f32)cost / size;
}

static bool memShouldMergePages(const M3Memory* mem) {
    const f32 currentOverhead = calculateOverhead(mem->_numPages, mem->pagesWithData, mem->pageSize);
    if (currentOverhead < mem->mergeThreshold) {
        return false;
    }

    const f32 previewOverhead = calculateOverhead(mem->_numPages, mem->pagesWithData - 1, mem->pageSize);
    return previewOverhead < currentOverhead;
}

void memMergePages(M3Memory* mem) {

}

void memStore(M3Memory* mem, const u32 offset, const void* data, const u32 size) {
    if (size == 0) return;
    if (offset + size > mem->numPages * PAGE_SIZE) {
        // TODO: Out of bounds
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

    if (mem->mergeThreshold > 0 && memShouldMergePages(mem)) {
        memMergePages(mem);
    }
}

void* memLoad(const M3Memory* mem, const u32 offset, const u32 size) {
    if (size == 0) return NULL;
    if (offset + size > mem->numPages * PAGE_SIZE) {
        // TODO: Out of bounds
    }

    void* data = m3_Malloc("memory load", size);
    memset(data, 0, size);

    u32 pageIdx = offset / mem->pageSize;
    u32 pageOff = offset % mem->pageSize;
    u32 written = 0;
    while (written < size) {
        const u32 n = min(size-written, mem->pageSize-pageOff);
        memLoadFromPage(mem, pageIdx, pageOff, data + written, n);
        written += n;
        pageIdx++;
        pageOff = 0;
    }

    return data;
}