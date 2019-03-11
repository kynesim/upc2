/* srec.h */
/* Copyright (c) Kynesim Ltd, 2019 */

#ifndef SREC_H_INCLUDED
#define SREC_H_INCLUDED

#include <stdint.h>


/* Return values of read_srec() */
#define SREC_ERROR_SUCCESS           0
#define SREC_ERROR_MISSING_STRUCT    1
#define SREC_ERROR_IO_ERROR          2
#define SREC_ERROR_INVALID_CHARACTER 3
#define SREC_ERROR_INVALID_TYPE      4
#define SREC_ERROR_UNEXPECTED_EOF    5
#define SREC_ERROR_BAD_BYTE_COUNT    6
#define SREC_ERROR_BAD_ADDRESS       7
#define SREC_ERROR_BAD_DATA          8
#define SREC_ERROR_BAD_CHECKSUM      9
#define SREC_ERROR_INVALID_CHECKSUM 10

typedef struct srec_struct
{
    uint8_t type;
    uint8_t byte_count;
    uint32_t address;
    uint8_t data[256];
} srec_t;


int read_srec(int fd, srec_t *srec);


#endif /* SREC_H_INCLUDED */
