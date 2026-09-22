/*
 * Block coverage for the harness: plant a one-shot INT3 at every basic block
 * start (list produced by tools/blocklist.py); a vectored exception handler
 * records the hit, restores the original byte and resumes.  Each block costs
 * one exception the first time it runs, nothing afterwards.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include "coverage.h"

typedef struct {
    uint32_t va, func;
    uint8_t orig;
    uint8_t hit;
} cov_block;

static cov_block *g_blocks;
static int g_nblocks;

static int cmp_block(const void *a, const void *b)
{
    uint32_t x = ((const cov_block *)a)->va, y = ((const cov_block *)b)->va;
    return x < y ? -1 : x > y;
}

static cov_block *find(uint32_t va)
{
    int lo = 0, hi = g_nblocks - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (g_blocks[mid].va == va)
            return &g_blocks[mid];
        if (g_blocks[mid].va < va)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return NULL;
}

static LONG CALLBACK cov_handler(EXCEPTION_POINTERS *ep)
{
    cov_block *b;
    uint32_t va;
    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_BREAKPOINT)
        return EXCEPTION_CONTINUE_SEARCH;
    va = (uint32_t)(uintptr_t)ep->ExceptionRecord->ExceptionAddress;
    b = find(va);
    if (!b || b->hit)
        return EXCEPTION_CONTINUE_SEARCH;
    b->hit = 1;
    *(uint8_t *)(uintptr_t)va = b->orig;
    ep->ContextRecord->Eip = va;
    return EXCEPTION_CONTINUE_EXECUTION;
}

int cov_load(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[128];
    int cap = 0;
    if (!f) {
        fprintf(stderr, "coverage: cannot open %s\n", path);
        return -1;
    }
    while (fgets(line, sizeof line, f)) {
        unsigned va, fn;
        if (sscanf(line, "%x %x", &va, &fn) != 2)
            continue;
        if (g_nblocks == cap) {
            cap = cap ? cap * 2 : 4096;
            g_blocks = (cov_block *)realloc(g_blocks, cap * sizeof *g_blocks);
        }
        g_blocks[g_nblocks].va = va;
        g_blocks[g_nblocks].func = fn;
        g_blocks[g_nblocks].hit = 0;
        g_nblocks++;
    }
    fclose(f);
    qsort(g_blocks, g_nblocks, sizeof *g_blocks, cmp_block);
    return 0;
}

void cov_arm(void)
{
    int i;
    AddVectoredExceptionHandler(1, cov_handler);
    for (i = 0; i < g_nblocks; i++) {
        uint8_t *p = (uint8_t *)(uintptr_t)g_blocks[i].va;
        g_blocks[i].orig = *p;
        *p = 0xCC;
    }
    FlushInstructionCache(GetCurrentProcess(), NULL, 0);
}

void cov_disarm(void)
{
    int i;
    for (i = 0; i < g_nblocks; i++)
        if (!g_blocks[i].hit)
            *(uint8_t *)(uintptr_t)g_blocks[i].va = g_blocks[i].orig;
}

int cov_write(const char *path)
{
    FILE *f = fopen(path, "w");
    int i, n = 0;
    if (!f)
        return -1;
    for (i = 0; i < g_nblocks; i++)
        if (g_blocks[i].hit) {
            fprintf(f, "%08x %08x\n", g_blocks[i].va, g_blocks[i].func);
            n++;
        }
    fclose(f);
    fprintf(stderr, "coverage: %d of %d blocks hit\n", n, g_nblocks);
    return 0;
}
