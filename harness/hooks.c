/*
 * Hook installation for the hook build: patch the entry of each original
 * function that has a decompiled C replacement with `jmp replacement`.
 *
 * spec: "all" | "none" | comma-separated names/addresses to enable, or
 *       names/addresses prefixed with '-' to disable from "all".
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hooks.h"

static int listed(const char *spec, const hook_entry *h, int *negated)
{
    const char *p = spec;
    char tok[128];
    *negated = 0;
    while (*p) {
        size_t n = strcspn(p, ",");
        int neg = 0;
        const char *t = p;
        if (n >= sizeof tok)
            n = sizeof tok - 1;
        if (*t == '-') { neg = 1; t++; n--; }
        memcpy(tok, t, n);
        tok[n] = 0;
        if (!strcmp(tok, h->name) || strtoul(tok, NULL, 16) == h->addr) {
            *negated = neg;
            return 1;
        }
        p = t + n;
        if (*p == ',')
            p++;
    }
    return 0;
}

int hooks_install(const char *spec, int verbose)
{
    const hook_entry *h;
    int n = 0, total = 0;
    int all = spec && !strncmp(spec, "all", 3);
    for (h = tv_hooks; h->addr; h++) {
        int neg, on;
        uint8_t *p = (uint8_t *)(uintptr_t)h->addr;
        total++;
        if (!spec || !strcmp(spec, "none"))
            on = 0;
        else if (all)
            on = !(listed(spec, h, &neg) && neg);
        else
            on = listed(spec, h, &neg) && !neg;
        if (!on)
            continue;
        p[0] = 0xE9;
        *(int32_t *)(p + 1) = (int32_t)((uint8_t *)h->fn - (p + 5));
        n++;
        if (verbose)
            fprintf(stderr, "hook %08x -> %s\n", (unsigned)h->addr, h->name);
    }
    FlushInstructionCache(GetCurrentProcess(), NULL, 0);
    fprintf(stderr, "hooks: %d of %d installed\n", n, total);
    return n;
}
