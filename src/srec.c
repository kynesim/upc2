/* srec.c */
/* Copyright (c) Kynesim Ltd, 2019 */

#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <ctype.h>

#include "upc2/srec.h"
#include "upc2/utils.h"


static int read_or_die(int fd, uint8_t *buf, size_t nbytes)
{
    ssize_t nread = 0;
    ssize_t rv;

    while (nread < nbytes)
    {
        rv = utils_safe_read(fd, buf + nread, nbytes - nread);
        if (rv < 0)
            return -1;
        if (rv == 0)
            return 0;
        nread += rv;
    }
    return nread;
}


int read_srec(int fd, srec_t *srec)
{
    uint8_t buf[9];
    int rv;
    uint8_t checksum;
    uint8_t data_checksum;
    char *endp;
    size_t nread;
    unsigned int i;

    if (srec == NULL)
        return SREC_ERROR_MISSING_STRUCT;

    while (1)
    {
        if ((rv = utils_safe_read(fd, buf, 1)) < 0)
            return SREC_ERROR_IO_ERROR;
        if (rv == 0)
        {
            srec->byte_count = 0;
            return SREC_ERROR_SUCCESS;
        }
        if (*buf == 'S')
            break;
        if (!isspace(*buf))
            return SREC_ERROR_INVALID_CHARACTER;
    }

    /* Read the record type byte */
    if ((rv = utils_safe_read(fd, &srec->type, 1)) < 0)
        return SREC_ERROR_IO_ERROR;
    if (!isxdigit(srec->type) || srec->type == '4')
        return SREC_ERROR_INVALID_TYPE;

    /* Read the byte count */
    if ((rv = read_or_die(fd, buf, 2)) < 0)
        return SREC_ERROR_IO_ERROR;
    else if (rv == 0)
        return SREC_ERROR_UNEXPECTED_EOF;
    buf[2] = '\0';
    srec->byte_count = strtoul((char *)buf, &endp, 16);
    if (*endp != '\0')
        return SREC_ERROR_BAD_BYTE_COUNT;
    checksum = srec->byte_count;

    /* Find out how many bytes in the address */
    switch (srec->type)
    {
        case '0': case '1': case '5': case '9':
            nread = 4;
            break;

        case '2': case '6': case '8':
            nread = 6;
            break;

        default:
            nread = 8;
    }
    if ((rv = read_or_die(fd, buf, nread)) < 0)
        return SREC_ERROR_IO_ERROR;
    else if (rv == 0)
        return SREC_ERROR_UNEXPECTED_EOF;
    buf[nread] = '\0';
    srec->address = strtoul((char *)buf, &endp, 16);
    if (*endp != '\0')
        return SREC_ERROR_BAD_ADDRESS;
    checksum += srec->address & 0xff;
    checksum += (srec->address >> 8) & 0xff;
    if (nread >= 6)
        checksum += (srec->address >> 16) & 0xff;
    if (nread == 8)
        checksum += (srec->address >> 24) & 0xff;

    /* Adjust the advertised byte count to just the data bytes */
    srec->byte_count -= nread/2 + 1;

    for (i = 0; i < srec->byte_count; i++)
    {
        if ((rv = read_or_die(fd, buf, 2)) < 0)
            return SREC_ERROR_IO_ERROR;
        else if (rv == 0)
            return SREC_ERROR_UNEXPECTED_EOF;
        buf[2] = '\0';
        srec->data[i] = strtoul((char *)buf, &endp, 16);
        if (*endp != '\0')
            return SREC_ERROR_BAD_DATA;
        checksum += srec->data[i];
    }

    /* Finally read the checksum */
    if ((rv = read_or_die(fd, buf, 2)) < 0)
        return SREC_ERROR_IO_ERROR;
    else if (rv == 0)
        return SREC_ERROR_UNEXPECTED_EOF;
    buf[2] = '\0';
    data_checksum = strtoul((char *)buf, &endp, 16);
    if (*endp != '\0')
        return SREC_ERROR_BAD_CHECKSUM;
    if ((data_checksum ^ checksum) != 0xff)
        return SREC_ERROR_INVALID_CHECKSUM;

    return SREC_ERROR_SUCCESS;
}
