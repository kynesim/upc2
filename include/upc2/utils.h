/* utils.h */
/* (C) Kynesim Ltd 2012-5 */

#ifndef UTILS_H_INCLUDED
#define UTILS_H_INCLUDED

#include <stdint.h>
#include "upc2/up.h"

/* Ensure all 'len' bytes are written to the fd, or error */
int utils_safe_write(int fd, const uint8_t *data, int len);

/* Non-blocking read that retries EINTR and EAGAIN "errors" */
int utils_safe_read(int fd, uint8_t *data, int len);

/* Check if there has been a control character - the only thing this
 *  checks for is critical control characters, like C-a x, so you
 *  can abort failed grouches.
 *
 *  Also sets up->ctrl appropriately so that split control commands
 * work properly (C-a in grouch, then h after).
 */
int utils_check_critical_control(up_context_t *up);

/* safe_write() for bios */
int utils_bio_safe_write(up_bio_t *bio, const uint8_t *data, int nr);

/* safe_write for console with printf semantics */
int utils_safe_printf(up_context_t *ctx, const char *str, ...);

/* set_baud as a protocol "prepare" entry point */
int utils_protocol_set_baud(void *h, up_context_t *ctx, up_load_arg_t *arg);

#endif

/* End file */

