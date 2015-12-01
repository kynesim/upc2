/* utils.h */
/* (C) Kynesim Ltd 2012-5 */

#ifndef UTILS_H_INCLUDED
#define UTILS_H_INCLUDED

#include <stdint.h>
#include "upc2/up.h"

int utils_safe_write(int fd, const uint8_t *data, int len);

/* Check if there has been a control character - the only thing this
 *  checks for is critical control characters, like C-a C-x, so you
 * can abort failed grouches.
 *
 *  Also sets up->ctrl appropriately so that split control commands
 * work properly (C-a in grouch, then h after).
 */
int utils_check_critical_control(up_context_t *up);

/* safe_write() for bios */
int utils_bio_safe_write(up_bio_t *bio, const uint8_t *data, int nr);

#endif

/* End file */

