/* grouch.h */
/* Copyright (c) Kynesim Ltd, 2012-5 */

#ifndef GROUCH_H_INCLUDED
#define GROUCH_H_INCLUDED

#include "upc2/up.h"

/* Attempts to start grouchloading given a buffer of serial input.
 * Returns 0 if the "*LOAD*" cue has not yet been seen, -1 on error or
 * 1 if the grouchload has been completed successfully.
 */
extern int maybe_grouch(up_context_t  *ctx,
                        up_load_arg_t *arg,
                        const uint8_t *buf,
                        int            rv);

#endif /* GROUCH_H_INCLUDED */

