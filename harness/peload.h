#ifndef PELOAD_H
#define PELOAD_H
#include <stdint.h>

typedef struct {
    uint8_t *base;      /* where the image is mapped */
    uint32_t size;      /* SizeOfImage */
    uint32_t pref_base; /* ImageBase from the header */
    void *entry;        /* DllMain / entry point, or NULL */
} pe_image;

/* Called for every import; returns the address to store in the IAT slot.
 * name is NULL for imports by ordinal (ord >= 0). */
typedef void *(*pe_resolver)(const char *dll, const char *name, int ord, void *ctx);

int pe_load(const char *path, pe_image *img, pe_resolver resolve, void *ctx);
int pe_call_entry(pe_image *img, uint32_t reason);

/* Translate a virtual address from the listings (preferred base) to a pointer. */
void *pe_va(pe_image *img, uint32_t va);

#endif
