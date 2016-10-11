/* up_lineend.h */
/* Copyright (c) Kynesim Ltd, 2012-16 */

#ifndef UP_LINEEND_H_INCLUDED
#define UP_LINEEND_H_INCLUDED

/** @file
 *
 * API for translating line endings (and possibly other things)
 *
 * @author Rhodri James <rhodri@kynesim.co.uk>
 * @date   2016-10-10
 */

#include <stdint.h>

/* Forward declaration */
typedef struct up_translation_struct up_translation_t;

/** Translation function */
typedef int (*translate)(uint8_t *out_buf,
                         const uint8_t *in_buf,
                         int in_buf_bytes);

/** Translation state machine function
 *
 * @param  [in]  in_byte  Character to run through state machine
 * @param  [in,out] trans  Translation state structure
 *
 * @return Output byte in low eight bits, output flags in upper 24 bits
 */
typedef uint32_t (*trans_next)(uint8_t in_byte, up_translation_t *trans);

/** Flag indicating no output byte from trans_next() */
#define TN_SUPPRESS   (0x80000000)
/** Flag indicating trans_next() should be called again for another byte */
#define TN_CALL_AGAIN (0x40000000)


typedef struct up_translation_struct {
    /** Translation function */
    trans_next translate;
    /** Internal state */
    uint32_t state;
} up_translation_t;


typedef struct up_translation_table_struct {
    /** Name of command line 'lineend' parameter value */
    const char *command_string;
    /** Two character escape sequence after ^A^L */
    const char escape_sequence[2];
    /** Host->serial translation function corresponding to this entry */
    up_translation_t from_serial;
    /** Serial->host translation function corresponding to this entry */
    up_translation_t to_serial;
} up_translation_table_t;


extern up_translation_table_t *parse_line_end(const char *name);
extern int translate_buffer(uint8_t *out_buf,
                            const uint8_t *in_buf,
                            int in_buf_bytes,
                            up_translation_t *trans);

#endif /* UP_LINEEND_H_INCLUDED */
