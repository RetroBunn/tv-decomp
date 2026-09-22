#ifndef COVERAGE_H
#define COVERAGE_H
#include <stdint.h>

/* Load "<block va> <func va>" lines, plant INT3s, and dump hits. */
int cov_load(const char *path);
void cov_arm(void);
void cov_disarm(void);
int cov_write(const char *path);

#endif
