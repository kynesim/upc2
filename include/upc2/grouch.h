/* grouch.h */
/* Copyright (c) Kynesim Ltd, 2012-5 */

#ifndef GROUCH_H_INCLUDED
#define GROUCH_H_INCLUDED

#include "upc2/up.h"

extern int maybe_grouch(up_context_t  *ctx,
                        up_load_arg_t *arg,
                        const uint8_t *buf,
                        int            rv);

extern int up_internal_check_control(up_context_t *ctx);


#endif /* GROUCH_H_INCLUDED */
