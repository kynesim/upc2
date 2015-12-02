/* xmodem.h */
/* (C) Kynesim Ltd 2012-13 */

#ifndef XMODEM_H_INCLUDED
#define XMODEM_H_INCLUDED

#include "upc2/up.h"

/* Sends the relevant file using the XModem protocol.
 *
 * Returns a negative number on failure/abort, or 1 once the
 * transfer is complete.
 */
extern int xmodem_boot(up_context_t *ctx, up_load_arg_t *arg);

#endif


/* End file */
