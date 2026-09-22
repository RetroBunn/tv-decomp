/*
 * Minimal PE32 loader for the test harness.
 *
 * Maps a DLL into memory ourselves (instead of LoadLibrary) so that every
 * import can be routed through a thunk we control: traced, replaced by a
 * sandbox stub, or trapped.  The image is placed at its preferred base when
 * possible so addresses match the disassembly listings.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "peload.h"

static uint8_t *read_file(const char *path, uint32_t *size)
{
    FILE *f = fopen(path, "rb");
    uint8_t *buf;
    long n;
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (uint8_t *)malloc(n);
    if (buf && fread(buf, 1, n, f) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    *size = (uint32_t)n;
    return buf;
}

int pe_load(const char *path, pe_image *img, pe_resolver resolve, void *ctx)
{
    uint32_t fsize, i;
    uint8_t *file = read_file(path, &fsize);
    IMAGE_DOS_HEADER *dos;
    IMAGE_NT_HEADERS32 *nt;
    IMAGE_SECTION_HEADER *sec;
    uint8_t *base;
    uint32_t delta;

    memset(img, 0, sizeof *img);
    if (!file) {
        fprintf(stderr, "pe_load: cannot read %s\n", path);
        return -1;
    }
    dos = (IMAGE_DOS_HEADER *)file;
    nt = (IMAGE_NT_HEADERS32 *)(file + dos->e_lfanew);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386) {
        fprintf(stderr, "pe_load: %s is not a PE32 i386 image\n", path);
        free(file);
        return -1;
    }

    base = (uint8_t *)VirtualAlloc((void *)(uintptr_t)nt->OptionalHeader.ImageBase,
                                   nt->OptionalHeader.SizeOfImage,
                                   MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (!base)
        base = (uint8_t *)VirtualAlloc(NULL, nt->OptionalHeader.SizeOfImage,
                                       MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (!base) {
        fprintf(stderr, "pe_load: VirtualAlloc failed\n");
        free(file);
        return -1;
    }

    memcpy(base, file, nt->OptionalHeader.SizeOfHeaders);
    sec = IMAGE_FIRST_SECTION(nt);
    for (i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        uint32_t n = sec[i].SizeOfRawData;
        if (n > sec[i].Misc.VirtualSize && sec[i].Misc.VirtualSize)
            n = sec[i].Misc.VirtualSize;
        if (n)
            memcpy(base + sec[i].VirtualAddress, file + sec[i].PointerToRawData, n);
    }
    free(file);
    nt = (IMAGE_NT_HEADERS32 *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);

    /* Base relocations (only needed if we did not get the preferred base). */
    delta = (uint32_t)(uintptr_t)base - nt->OptionalHeader.ImageBase;
    if (delta) {
        IMAGE_DATA_DIRECTORY *rd = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
        uint8_t *p = base + rd->VirtualAddress, *end = p + rd->Size;
        while (p < end) {
            IMAGE_BASE_RELOCATION *blk = (IMAGE_BASE_RELOCATION *)p;
            uint16_t *e = (uint16_t *)(blk + 1);
            uint32_t cnt = (blk->SizeOfBlock - sizeof *blk) / 2;
            if (!blk->SizeOfBlock)
                break;
            for (i = 0; i < cnt; i++)
                if ((e[i] >> 12) == IMAGE_REL_BASED_HIGHLOW)
                    *(uint32_t *)(base + blk->VirtualAddress + (e[i] & 0xfff)) += delta;
            p += blk->SizeOfBlock;
        }
    }

    /* Imports. */
    {
        IMAGE_DATA_DIRECTORY *id = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        IMAGE_IMPORT_DESCRIPTOR *d = (IMAGE_IMPORT_DESCRIPTOR *)(base + id->VirtualAddress);
        for (; id->Size && d->Name; d++) {
            const char *dll = (const char *)(base + d->Name);
            uint32_t *ilt = (uint32_t *)(base + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
            uint32_t *iat = (uint32_t *)(base + d->FirstThunk);
            for (; *ilt; ilt++, iat++) {
                const char *name = NULL;
                int ord = -1;
                void *addr;
                if (*ilt & IMAGE_ORDINAL_FLAG32)
                    ord = (int)(*ilt & 0xffff);
                else
                    name = (const char *)(base + *ilt + 2);
                addr = resolve(dll, name, ord, ctx);
                if (!addr) {
                    fprintf(stderr, "pe_load: unresolved import %s!%s\n", dll, name ? name : "(ordinal)");
                    return -1;
                }
                *iat = (uint32_t)(uintptr_t)addr;
            }
        }
    }

    img->base = base;
    img->size = nt->OptionalHeader.SizeOfImage;
    img->pref_base = nt->OptionalHeader.ImageBase;
    img->entry = nt->OptionalHeader.AddressOfEntryPoint ? base + nt->OptionalHeader.AddressOfEntryPoint : NULL;
    FlushInstructionCache(GetCurrentProcess(), base, img->size);
    return 0;
}

int pe_call_entry(pe_image *img, uint32_t reason)
{
    typedef BOOL(WINAPI * dllmain_t)(HINSTANCE, DWORD, LPVOID);
    if (!img->entry)
        return 1;
    return ((dllmain_t)img->entry)((HINSTANCE)img->base, reason, NULL);
}

void *pe_va(pe_image *img, uint32_t va)
{
    return img->base + (va - img->pref_base);
}
