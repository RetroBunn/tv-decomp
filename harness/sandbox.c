/*
 * Import sandbox for the test harness.
 *
 * Every import of the loaded DLL is bound to a small thunk:
 *     push <index> ; call sb_hook ; add esp,4 ; jmp [target]
 * sb_hook counts/logs the call (and aborts for trapped imports); target is
 * either the real Win32 function or one of the stubs below.
 *
 * Policy: the registry and file system are never touched (the DLL sees
 * "not found", i.e. factory defaults, and cannot modify the user's
 * installed engine settings); window/GDI/COM calls trap because the
 * engine-level harness never needs them.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sandbox.h"

#define MAX_IMPORTS 512

typedef enum { POL_REAL, POL_STUB, POL_TRAP } policy_t;

typedef struct {
    char dll[32];
    char name[64];
    policy_t pol;
    void *target;
    unsigned long calls;
} import_ent;

static import_ent g_imp[MAX_IMPORTS];
static int g_nimp;
static uint8_t *g_thunks;
static int g_trace;
static uint8_t *g_image_base;
static char g_fake_module_path[MAX_PATH] = "C:\\TruVoice\\cgrm_en.dll";

void sb_set_trace(int on) { g_trace = on; }

static void __cdecl sb_hook(int idx)
{
    import_ent *e = &g_imp[idx];
    e->calls++;
    if (e->pol == POL_TRAP) {
        fprintf(stderr, "sandbox: trapped call to %s!%s\n", e->dll, e->name);
        fflush(stderr);
        ExitProcess(3);
    }
    if (g_trace)
        fprintf(stderr, "[imp] %s!%s\n", e->dll, e->name);
}

/* ---- stubs (must match the real calling convention exactly) ---------- */

static LONG WINAPI stub_RegOpenKeyA(HKEY k, LPCSTR sub, PHKEY out)
{
    (void)k;
    if (g_trace) fprintf(stderr, "sandbox: RegOpenKeyA(%s) -> not found\n", sub ? sub : "");
    if (out) *out = NULL;
    return ERROR_FILE_NOT_FOUND;
}

static LONG WINAPI stub_RegCreateKeyA(HKEY k, LPCSTR sub, PHKEY out)
{
    (void)k;
    fprintf(stderr, "sandbox: RegCreateKeyA(%s) denied\n", sub ? sub : "");
    if (out) *out = NULL;
    return ERROR_ACCESS_DENIED;
}

static LONG WINAPI stub_RegQueryValueExA(HKEY k, LPCSTR v, LPDWORD r, LPDWORD t, LPBYTE d, LPDWORD n)
{
    (void)k; (void)v; (void)r; (void)t; (void)d; (void)n;
    return ERROR_FILE_NOT_FOUND;
}

static LONG WINAPI stub_RegSetValueExA(HKEY k, LPCSTR v, DWORD r, DWORD t, const BYTE *d, DWORD n)
{
    (void)k; (void)r; (void)t; (void)d; (void)n;
    fprintf(stderr, "sandbox: RegSetValueExA(%s) denied\n", v ? v : "");
    return ERROR_ACCESS_DENIED;
}

static LONG WINAPI stub_RegCloseKey(HKEY k) { (void)k; return ERROR_SUCCESS; }

static HFILE WINAPI stub_OpenFile(LPCSTR name, LPOFSTRUCT of, UINT style)
{
    (void)of;
    fprintf(stderr, "sandbox: OpenFile(%s, 0x%x) denied\n", name ? name : "", style);
    return HFILE_ERROR;
}

static HANDLE WINAPI stub_CreateFileA(LPCSTR name, DWORD acc, DWORD share, LPSECURITY_ATTRIBUTES sa,
                                      DWORD disp, DWORD flags, HANDLE tmpl)
{
    (void)acc; (void)share; (void)sa; (void)disp; (void)flags; (void)tmpl;
    fprintf(stderr, "sandbox: CreateFileA(%s) denied\n", name ? name : "");
    SetLastError(ERROR_FILE_NOT_FOUND);
    return INVALID_HANDLE_VALUE;
}

static DWORD WINAPI stub_GetModuleFileNameA(HMODULE m, LPSTR buf, DWORD n)
{
    if ((uint8_t *)m == g_image_base) {
        size_t len = strlen(g_fake_module_path);
        if (n == 0) return 0;
        if (len >= n) len = n - 1;
        memcpy(buf, g_fake_module_path, len);
        buf[len] = 0;
        return (DWORD)len;
    }
    return GetModuleFileNameA(m, buf, n);
}

static int WINAPI stub_MessageBoxA(HWND w, LPCSTR text, LPCSTR cap, UINT type)
{
    (void)w; (void)type;
    fprintf(stderr, "sandbox: MessageBoxA [%s] %s\n", cap ? cap : "", text ? text : "");
    return IDOK;
}

static unsigned long g_posted;
static BOOL WINAPI stub_PostMessageA(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    g_posted++;
    if (g_trace)
        fprintf(stderr, "sandbox: PostMessageA(%p, 0x%x, 0x%lx, 0x%lx)\n", (void *)w, msg,
                (unsigned long)wp, (unsigned long)lp);
    return TRUE;
}

typedef struct { const char *dll, *name; policy_t pol; void *stub; } rule;

static const rule g_rules[] = {
    {"ADVAPI32", "RegOpenKeyA", POL_STUB, (void *)stub_RegOpenKeyA},
    {"ADVAPI32", "RegCreateKeyA", POL_STUB, (void *)stub_RegCreateKeyA},
    {"ADVAPI32", "RegQueryValueExA", POL_STUB, (void *)stub_RegQueryValueExA},
    {"ADVAPI32", "RegSetValueExA", POL_STUB, (void *)stub_RegSetValueExA},
    {"ADVAPI32", "RegCloseKey", POL_STUB, (void *)stub_RegCloseKey},
    {"KERNEL32", "OpenFile", POL_STUB, (void *)stub_OpenFile},
    {"KERNEL32", "CreateFileA", POL_STUB, (void *)stub_CreateFileA},
    {"KERNEL32", "GetModuleFileNameA", POL_STUB, (void *)stub_GetModuleFileNameA},
    {"KERNEL32", "CreateThread", POL_TRAP, NULL},
    {"KERNEL32", "_lwrite", POL_TRAP, NULL},
    {"KERNEL32", "_lclose", POL_TRAP, NULL},
    {"USER32", "MessageBoxA", POL_STUB, (void *)stub_MessageBoxA},
    {"USER32", "PostMessageA", POL_STUB, (void *)stub_PostMessageA},
    {"USER32", "wsprintfA", POL_REAL, NULL},
    {NULL, NULL, POL_TRAP, NULL}
};

static int dll_is(const char *dll, const char *want)
{
    size_t n = strlen(want);
    return _strnicmp(dll, want, n) == 0 && (dll[n] == 0 || dll[n] == '.');
}

static void *sb_resolve(const char *dll, const char *name, int ord, void *ctx)
{
    import_ent *e;
    const rule *r;
    policy_t pol;
    void *target = NULL;
    uint8_t *t;
    (void)ctx;

    if (g_nimp >= MAX_IMPORTS)
        return NULL;
    e = &g_imp[g_nimp];
    _snprintf(e->dll, sizeof e->dll, "%s", dll);
    if (name)
        _snprintf(e->name, sizeof e->name, "%s", name);
    else
        _snprintf(e->name, sizeof e->name, "#%d", ord);

    /* default policy: kernel32 is real, everything else traps */
    pol = dll_is(dll, "KERNEL32") ? POL_REAL : POL_TRAP;
    for (r = g_rules; r->dll; r++)
        if (name && dll_is(dll, r->dll) && strcmp(name, r->name) == 0) {
            pol = r->pol;
            target = r->stub;
            break;
        }
    if (pol == POL_REAL) {
        HMODULE m = LoadLibraryA(dll);
        target = m ? (void *)GetProcAddress(m, name ? name : (LPCSTR)(uintptr_t)ord) : NULL;
        if (!target) {
            fprintf(stderr, "sandbox: cannot find real %s!%s, trapping\n", dll, e->name);
            pol = POL_TRAP;
        }
    }
    e->pol = pol;
    e->target = target;

    /* thunk: push idx ; call sb_hook ; add esp,4 ; jmp [&e->target] */
    t = g_thunks + g_nimp * 32;
    t[0] = 0x68;
    *(uint32_t *)(t + 1) = (uint32_t)g_nimp;
    t[5] = 0xE8;
    *(int32_t *)(t + 6) = (int32_t)((uint8_t *)sb_hook - (t + 10));
    t[10] = 0x83; t[11] = 0xC4; t[12] = 0x04;
    t[13] = 0xFF; t[14] = 0x25;
    *(uint32_t *)(t + 15) = (uint32_t)(uintptr_t)&e->target;
    t[19] = 0xCC;
    g_nimp++;
    return t;
}

int sb_load(const char *path, pe_image *img)
{
    if (!g_thunks) {
        g_thunks = (uint8_t *)VirtualAlloc(NULL, MAX_IMPORTS * 32, MEM_RESERVE | MEM_COMMIT,
                                           PAGE_EXECUTE_READWRITE);
        if (!g_thunks)
            return -1;
    }
    if (pe_load(path, img, sb_resolve, NULL) != 0)
        return -1;
    g_image_base = img->base;
    return 0;
}

void sb_report(void)
{
    int i;
    fprintf(stderr, "import call counts:\n");
    for (i = 0; i < g_nimp; i++)
        if (g_imp[i].calls)
            fprintf(stderr, "  %8lu %s!%s\n", g_imp[i].calls, g_imp[i].dll, g_imp[i].name);
    fprintf(stderr, "  (PostMessageA posted %lu)\n", g_posted);
}
