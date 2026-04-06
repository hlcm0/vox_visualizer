#pragma once

#include "model.h"

/* Parse a .vox file and populate chart.
 * Returns 0 on success, non-zero on failure. */
int vox_parse(const char *path, VoxChart *chart);
