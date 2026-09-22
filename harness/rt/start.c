/* Minimal process startup for freestanding 32-bit harness programs.
 * Uses the system msvcrt.dll for argv parsing and stdio. */
#include <stdlib.h>

typedef struct { int newmode; } startupinfo_t;
extern int __cdecl __getmainargs(int *argc, char ***argv, char ***env,
                                 int wildcard, startupinfo_t *si);
extern int main(int argc, char **argv);

void __main(void) {}

void __stdcall mainCRTStartup(void)
{
    int argc; char **argv, **env; startupinfo_t si = {0};
    __getmainargs(&argc, &argv, &env, 0, &si);
    exit(main(argc, argv));
}

/* The mingw headers reference UCRT's __acrt_iob_func for stdin/stdout/stderr;
 * map it onto msvcrt.dll's __iob_func (32-byte FILE structs). */
#include <stdio.h>
extern FILE *__cdecl __iob_func(void);
static FILE *__cdecl acrt_iob_func_shim(unsigned i) { return &__iob_func()[i]; }
FILE *(__cdecl *_imp____acrt_iob_func)(unsigned) = acrt_iob_func_shim;
