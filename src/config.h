/* /data/anypad/config.ini: the few things worth changing.
 *
 *   menu_combo   = options+l2+r2      buttons held to open the menu on the console
 *   menu_hold_ms = 1500                how long they are held (500..5000)
 *
 * Button names: l1 r1 l2 r2 l3 r3 cross circle square triangle up down left
 * right options create touchpad. At least two. The PS button cannot be read.
 * Lines starting with # are comments. A line that is not understood is
 * reported and skipped; the defaults stand for anything missing or wrong. */
#ifndef ANYPAD_CONFIG_H
#define ANYPAD_CONFIG_H

#include "pad.h"

#include <stddef.h>

typedef struct {
    uint32_t menu_combo;
    int menu_hold_ms;
} anypad_config;

void config_defaults(anypad_config *c);

/* Reads `path`. Returns the number of lines not understood; a missing file
 * is not an error (returns 0, defaults kept). */
int config_load(anypad_config *c, const char *path);

/* "options+l2+r2" -> mask. Returns 0 if a name is unknown or fewer than two
 * buttons are named. */
int config_parse_combo(const char *text, uint32_t *mask);

/* Mask -> words for a person: "L2 + R2 + OPTIONS". */
void config_combo_text(uint32_t mask, char *out, size_t max);

#endif
