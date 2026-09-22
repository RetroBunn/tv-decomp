/*
 * Small constant tables, defined in C for the standalone build.  (In the
 * hook build these symbols resolve to the originals inside the DLL, so this
 * directory is not compiled there.)
 */
#include "engine.h"

/* @0x100ec108 */
const uint32_t g_node_type_bits[8] = {0x10, 0x01, 0x02, 0x04, 0x08, 0x20, 0x00, 0x00};
