/* up_lineend.c */
/* Copyright (c) Kynesim Ltd, 2012-16 */

/** @file
 *
 * Handles line-end translations for UPC2 consoles
 *
 * @author Rhodri James <rhodri@kynesim.co.uk>
 * @date   2016-10-10
 */

#include <stdint.h>
#include <string.h>
#include "upc2/up_lineend.h"


static uint32_t tn_none(uint8_t in_byte, up_translation_t *trans)
{
    return in_byte;
}


#define TN_C2CL_NORMAL  0
#define TN_C2CL_SEEN_CR 1
static uint32_t tn_cr_to_crlf(uint8_t in_byte, up_translation_t *trans)
{
    if (trans->state == TN_C2CL_SEEN_CR)
    {
        trans->state = TN_C2CL_NORMAL;
        /* We ignore the input */
        return '\n';
    }
    else if (in_byte == '\r')
    {
        trans->state = TN_C2CL_SEEN_CR;
        return TN_CALL_AGAIN | '\r';
    }
    return in_byte;
}


#define TN_L2CL_NORMAL  0
#define TN_L2CL_SEEN_LF 1
static uint32_t tn_lf_to_crlf(uint8_t in_byte, up_translation_t *trans)
{
    if (trans->state == TN_L2CL_SEEN_LF)
    {
        trans->state = TN_L2CL_NORMAL;
        /* We ignore the input */
        return '\n';
    }
    else if (in_byte == '\n')
    {
        trans->state = TN_L2CL_SEEN_LF;
        return TN_CALL_AGAIN | '\r';
    }
    return in_byte;
}


#define TN_CL2C_NORMAL       0
#define TN_CL2C_SEEN_CR      1
static uint32_t tn_crlf_to_cr(uint8_t in_byte, up_translation_t *trans)
{
    if (trans->state == TN_CL2C_SEEN_CR)
    {
        /* If we have an LF here, ignore it.  Anything else indicates
         * a bare CR, which is curious but what the heck.
         */
        trans->state = TN_CL2C_NORMAL;
        if (in_byte == '\n')
            return TN_SUPPRESS;
        return in_byte;
    }
    else if (in_byte == '\r')
    {
        trans->state = TN_CL2C_SEEN_CR;
    }
    return in_byte;
}


static uint32_t tn_lf_to_cr(uint8_t in_byte, up_translation_t *trans)
{
    if (in_byte == '\n')
        return '\r';
    return in_byte;
}


#define TN_CL2L_NORMAL       0
#define TN_CL2L_SEEN_CR      1
#define TN_CL2L_SEEN_BARE_CR (0x80000000)
static uint32_t tn_crlf_to_lf(uint8_t in_byte, up_translation_t *trans)
{
    if ((trans->state & TN_CL2L_SEEN_BARE_CR) != 0)
    {
        /* This is a recovery state from seeing a CR without a
         * following LF.  The character that did follow instead is
         * encoded in the low 8 bits of the state word.
         */
        uint8_t result = trans->state & 0xff;
        trans->state = TN_CL2L_NORMAL;
        return result;
    }
    else if (trans->state == TN_CL2L_SEEN_CR)
    {
        if (in_byte != '\n')
        {
            /* We had a bare CR.  Send it and recover */
            trans->state = TN_CL2L_SEEN_BARE_CR | in_byte;
            return TN_CALL_AGAIN | '\r';
        }
        /* Otherwise return the LF we just saw */
        trans->state = TN_CL2L_NORMAL;
        return in_byte;
    }
    else if (in_byte == '\r')
    {
        trans->state = TN_CL2L_SEEN_CR;
        return TN_SUPPRESS;
    }
    return in_byte;
}


static uint32_t tn_cr_to_lf(uint8_t in_byte, up_translation_t *trans)
{
    if (in_byte == '\r')
        return '\n';
    return in_byte;
}


/* This table must have a "none" entry for the default null translation */
static up_translation_table_t translation_table[] =
{
    /* Cmdline    ^Ae            from_serial           to_serial     */
    { "crlf2cr", { 'n', 'c' }, { tn_cr_to_crlf, 0 }, { tn_crlf_to_cr, 0 }},
    { "crlf2lf", { 'n', 'l' }, { tn_lf_to_crlf, 0 }, { tn_crlf_to_lf, 0 }},
    { "cr2crlf", { 'c', 'n' }, { tn_crlf_to_cr, 0 }, { tn_cr_to_crlf, 0 }},
    { "cr2lf",   { 'c', 'l' }, { tn_lf_to_cr,   0 }, { tn_cr_to_lf,   0 }},
    { "lf2crlf", { 'l', 'n' }, { tn_crlf_to_lf, 0 }, { tn_lf_to_crlf, 0 }},
    { "lf2cr",   { 'l', 'c' }, { tn_cr_to_lf,   0 }, { tn_lf_to_cr,   0 }},
    /* Required default */
    { "none",    { 'n', 'n' }, { tn_none,       0 }, { tn_none,       0 }},
    /* Required terminator */
    { NULL }
};


up_translation_table_t *parse_line_end(const char *name)
{
    up_translation_table_t *p;

    for (p = translation_table; p->command_string != NULL; p++)
    {
        if (!strcmp(p->command_string, name))
        {
            p->from_serial.state = 0;
            p->to_serial.state = 0;
            return p;
        }
    }
    return NULL;
}


up_translation_table_t *parse_escape_line_end(uint8_t first,
                                              uint8_t second)
{
    up_translation_table_t *p;

    for (p = translation_table; p->command_string != NULL; p++)
    {
        if (first == p->escape_sequence[0] &&
            second == p->escape_sequence[1])
        {
            p->from_serial.state = 0;
            p->to_serial.state = 0;
            return p;
        }
    }
    return NULL;
}


int translate_buffer(uint8_t *out_buf,
                     const uint8_t *in_buf,
                     int in_buf_bytes,
                     up_translation_t *trans)
{
    const uint8_t *p = in_buf;
    uint8_t *q = out_buf;
    int out_bytes = in_buf_bytes;
    uint32_t result;

    while (in_buf_bytes-- > 0)
    {
        result = trans->translate(*p++, trans);
        while (result & TN_CALL_AGAIN)
        {
            if (result & TN_SUPPRESS)
                out_bytes--;
            else
                *q++ = result & 0xff;
            result = trans->translate('\0', trans);
            out_bytes++;
        }
        if (result & TN_SUPPRESS)
            out_bytes--;
        else
            *q++ = result & 0xff;
    }

    return out_bytes;
}
