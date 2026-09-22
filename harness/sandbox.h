#ifndef SANDBOX_H
#define SANDBOX_H
#include "peload.h"

/* Load an image with all imports routed through sandbox thunks. */
int sb_load(const char *path, pe_image *img);
void sb_set_trace(int on);
void sb_report(void);

#endif
