/* xmodem.c */
/* (C) Kynesim Ltd 2012 - 2013 */


/** @file
 *
 *  Operates enough of xmodem to boot an 8148 (or an AM335x)
 *  Heavily hacked by rrw.
 *
 * @author Rhodri James <rhodri@kynesim.co.uk>
 * @author Richard Watts <rrw@kynesim.co.uk>
 */


#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <sys/errno.h>
#include <unistd.h>
#include <stdlib.h>

#include "upc2/xmodem.h"
#include "upc2/up.h"
#include "upc2/utils.h"

#define FILE_NAME(x) \
    (((x)->file_name) ? ((x)->file_name) : "(no file name)")

#define DEBUG0 0

#define XBUFFER_BYTES (3 + 1024 + 2)
#define XBUFFER_SHORT_BYTES (3 + 128 + 2)
#define XBUFFER_HEADER_BYTES 3
#define XBUFFER_DATA_BYTES 1024
#define XBUFFER_SHORT_DATA_BYTES 128

#define XBUFFER_TYPE_OFS 0
#define XBUFFER_BLK_NO_OFS 1
#define XBUFFER_BLK_INV_OFS 2
#define XBUFFER_DATA_OFS XBUFFER_HEADER_BYTES
#define XBUFFER_CHECK_OFS (XBUFFER_HEADER_BYTES + XBUFFER_DATA_BYTES)
#define XBUFFER_SHORT_CHECK_OFS \
    (XBUFFER_HEADER_BYTES + XBUFFER_SHORT_DATA_BYTES)

#define XMODEM_PAD (0x1a)
#define XMODEM_TYPE_SHORT (0x01)
#define XMODEM_TYPE_LONG (0x02)
#define XMODEM_ACK (0x06)
#define XMODEM_NAK (0x15)
#define XMODEM_USE_CRC16 (0x43)
#define XMODEM_DONE (0x04)

static int xmodem_boot(void          *h,
                       up_context_t  *ctx,
                       up_load_arg_t *arg,
                       const uint8_t *buf,
                       int            buf_bytes);

static int xmodem128_boot(void          *h,
                       up_context_t  *ctx,
                       up_load_arg_t *arg,
                       const uint8_t *buf,
                       int            buf_bytes);

static int xmodem_transfer(void          *h,
                           up_context_t  *ctx,
                           up_load_arg_t *arg,
                           const uint8_t *buf,
                           int            buf_bytes,
                           int force_128);


const up_protocol_t xmodem_protocol = {
    "xmodem",
    NULL,
    utils_protocol_set_baud,
    xmodem_boot,
    NULL,
    NULL
};

const up_protocol_t xmodem128_protocol = {
    "xmodem128",
    NULL,
    utils_protocol_set_baud,
    xmodem128_boot,
    NULL,
    NULL
};


static int send_byte(up_context_t *upc, const uint8_t c);
static int get_byte(up_context_t *upc);

/*
 * Initialise CRC tables for CRC-16-CCITT
 */
static void create_crc_table(uint16_t *crc_table)
{
    uint16_t rem;
    uint8_t bit;
    int div;

    for (div = 0; div < 256; ++div)
    {
        rem = div << 8;
        for (bit = 8; bit > 0; --bit)
        {
            if (rem & 0x8000U)
                rem = (rem << 1) ^ 0x1021;
            else
                rem <<= 1;
        }
        crc_table[div] = rem;
    }
}


static void crc_buffer(uint8_t *buffer)
{
    int is_short = (buffer[XBUFFER_TYPE_OFS] == XMODEM_TYPE_SHORT);
    int data_len = is_short ?
        XBUFFER_SHORT_DATA_BYTES : XBUFFER_DATA_BYTES;
    char crc = 0;
    int i;

    buffer += XBUFFER_DATA_OFS;
    for (i = 0; i < data_len; i++)
        crc += *buffer++;

    *buffer = crc;
}

static void crc16_buffer(uint8_t *buffer, const uint16_t *crc_table)
{
    int data_len =
        (buffer[XBUFFER_TYPE_OFS] == XMODEM_TYPE_SHORT) ?
        XBUFFER_SHORT_DATA_BYTES : XBUFFER_DATA_BYTES;
    uint16_t crc = 0;
    int i;

    buffer += XBUFFER_DATA_OFS;
    for (i = 0; i < data_len; i++, ++buffer)
    {
        crc = crc_table[((uint8_t)*buffer ^ (crc >> 8)) & 0xff] ^ (crc << 8);
    }

    *buffer++ = crc >> 8;
    *buffer = crc & 0xff;
}

static uint32_t load_buffer(uint8_t *tx_buffer,
                            const uint8_t *image_buffer,
                            uint32_t image_bytes,
                            int blksz,
                            int blk)
{
    tx_buffer[1] = blk;
    tx_buffer[2] = 255 - blk;
    if ((blksz && blksz == XBUFFER_SHORT_DATA_BYTES) ||
        (!blksz && (image_bytes <= XBUFFER_SHORT_DATA_BYTES)))
    {
        int take = XBUFFER_SHORT_DATA_BYTES;
        if (image_bytes < take) { take = image_bytes; }
#if DEBUG0
        printf("load_buffer: Case A: %d \n", take);
#endif

        memcpy(&tx_buffer[XBUFFER_HEADER_BYTES], image_buffer, take);
        memset(&tx_buffer[XBUFFER_HEADER_BYTES] + take, 
               XMODEM_PAD,
               XBUFFER_SHORT_DATA_BYTES - take );
        tx_buffer[XBUFFER_TYPE_OFS] = XMODEM_TYPE_SHORT;
        return take;
    }
    else if (image_bytes <= XBUFFER_DATA_BYTES)
    {
        memcpy(&tx_buffer[XBUFFER_HEADER_BYTES], image_buffer, image_bytes);
        memset(&tx_buffer[XBUFFER_HEADER_BYTES] + image_bytes,
               XMODEM_PAD,
               XBUFFER_DATA_BYTES - image_bytes);
#if DEBUG0
        printf("load_buffer: Case B: %d \n", image_bytes);
#endif
        tx_buffer[XBUFFER_TYPE_OFS] = XMODEM_TYPE_LONG;
        return image_bytes;
    }
    /* Else... */
    memcpy(&tx_buffer[XBUFFER_HEADER_BYTES],
           image_buffer,
           XBUFFER_DATA_BYTES);
    tx_buffer[XBUFFER_TYPE_OFS] = XMODEM_TYPE_LONG;
#if DEBUG0
    printf("load_buffer: Case C: %d \n", XBUFFER_DATA_BYTES);
#endif

    return XBUFFER_DATA_BYTES;
}


static int send_buffer(up_context_t *ctx, uint8_t *buffer,
                       int use_crc16)
{
    int length = (buffer[XBUFFER_TYPE_OFS] == XMODEM_TYPE_SHORT) ?
        XBUFFER_SHORT_BYTES : XBUFFER_BYTES;
    int done = 0;

    if (!use_crc16)
        /* Only one byte of CRC */
        length--;

    while (done < length)
    {
        int rv = ctx->bio->write(ctx->bio, &buffer[done], length - done);
        if (rv < 0)
        {
            if (errno == EINTR || errno == EAGAIN)
                continue;
            return -1;
        }
        done += rv;
    }

    return -1;
}


static int xmodem_go(up_context_t  *ctx,
                     const uint8_t *boot_image,
                     const ssize_t  ib,
                     const int force_128)
{
    int done = 0;
    int rx_byte;
    int use_crc16;
    uint16_t crc_table[256];
    uint8_t buffer[XBUFFER_BYTES];
    uint32_t bytes_taken;
    int blk = 1; // First block in XModem is 1
    int blksz;
    ssize_t image_bytes = ib;

    create_crc_table(crc_table);

    // Set blksz non-zero to allow variable block sizes.
    // blksz = (image_bytes <= XBUFFER_SHORT_DATA_BYTES) ?
    //     XBUFFER_SHORT_DATA_BYTES : XBUFFER_DATA_BYTES;
    blksz = force_128 ? XBUFFER_SHORT_DATA_BYTES : 0;
#if DEBUG0
    printf("blksz = %d \n", blksz);
#endif

    /* Pre-load the buffer */
    bytes_taken = load_buffer(buffer, boot_image, image_bytes, blksz, blk);

    image_bytes -= bytes_taken;
    boot_image += bytes_taken;
    ++blk;

    /* Now wait for the client to start us off */
    while (1)
    {
        rx_byte = get_byte(ctx);
        if (rx_byte < 0) { return rx_byte; }
        if (rx_byte == XMODEM_NAK)
        {
#if DEBUG0
            printf("XMODEM_NAK \n");
#endif
            /* Use summing CRC */
            use_crc16 = 0;
            crc_buffer(buffer);
            break;
        }
        else if (rx_byte == XMODEM_USE_CRC16)
        {
#if DEBUG0
            printf("XMODEM_USE_CRC16\n");
#endif

            /* Do what it says */
            use_crc16 = 1;
            crc16_buffer(buffer, crc_table);
            break;
        }
        else
        {
            uint8_t c = rx_byte;
            printf("c = %02x \n", c);
            utils_safe_write(ctx->ttyfd, &c, 1);
        }
    }

    utils_safe_printf(ctx,
                      "[[ XMODEM start detected. Uploading %d bytes. ]]\n",
                      (int)ib);

    /* We are started, send everything */
    while (!done)
    {
#if DEBUG0
        printf("Sending buffer...\n");
#endif
        if (utils_check_critical_control(ctx) < 0)
            return -2;

        utils_safe_printf(ctx, "[[ Send %d bytes (%d remain) ]]\n",
                          (int)bytes_taken, (int)image_bytes);
        send_buffer(ctx, buffer, use_crc16);

        /* Wait to see if the 8148 liked it */
        rx_byte = get_byte(ctx);
        if (rx_byte < 0)
            return rx_byte;

#if DEBUG0
        printf("rx_byte = 0x%02x \n", rx_byte);
#endif

        /* Consider anything other than ACK as requiring a resend */
        if (rx_byte != XMODEM_ACK)
            continue;  /* Resend the same buffer */

        if (image_bytes != 0)
        {
            bytes_taken = load_buffer(buffer,
                                      boot_image,
                                      image_bytes, blksz,  blk);
            printf("blksz[2] = %d \n", blksz);
            ++blk;
            image_bytes -= bytes_taken;
            boot_image += bytes_taken;
            if (use_crc16)
                crc16_buffer(buffer, crc_table);
            else
                crc_buffer(buffer);
        }
        else
        {
            done = 1;
        }
    }

#if DEBUG0
    printf("XMODEM_DONE.\n");
#endif

    send_byte(ctx, XMODEM_DONE);
    utils_safe_printf(ctx, "[[ XMODEM complete ]]\n");

    /* Download finished */
    return 1;
}


static int get_byte(up_context_t *upc)
{
    uint8_t c;
    int rv;
    while (1)
    {
        if (utils_check_critical_control(upc) < 0)
            return -3;
        rv = upc->bio->read(upc->bio, &c, 1);
        if (rv < 0)
        {
            if (errno == EINTR || errno == EAGAIN)
                continue;
            return rv;
        }
        else if (rv == 1)
        {
            return c;
        }
        else
        {
            return -2;
        }
    }
}

static int send_byte(up_context_t *upc, const uint8_t c)
{
    int rv = upc->bio->safe_write(upc->bio, &c, 1);

    return (rv < 0) ? rv : 0;
}

static int xmodem_boot(void          *h,
                       up_context_t  *ctx,
                       up_load_arg_t *arg,
                       const uint8_t *buf,
                       int            buf_bytes) {
    return xmodem_transfer(h, ctx, arg, buf, buf_bytes, 0);
}

static int xmodem128_boot(void          *h,
                       up_context_t  *ctx,
                       up_load_arg_t *arg,
                       const uint8_t *buf,
                          int            buf_bytes) {
    return xmodem_transfer(h, ctx, arg, buf, buf_bytes, 1);
}



static int xmodem_transfer(void          *h,
                           up_context_t  *ctx,
                           up_load_arg_t *arg,
                           const uint8_t *serial_in_buf,
                           int            buf_bytes,
                           int force_128)
{
    off_t of;
    uint32_t nr_bytes;
    uint8_t *buf = NULL;
    uint32_t done = 0;
    int rv;

    of = lseek(arg->fd, 0, SEEK_END);
    if (of == (off_t)-1)
    {
#if DEBUG0
        fprintf(stderr,
                "Cannot seek to end of boot file %s: %s [%d] \n",
                FILE_NAME(arg), strerror(errno), errno);
#endif
        return -1;
    }

    nr_bytes  = (uint32_t)of;

    of = lseek(arg->fd, 0, SEEK_SET);
    if (of == (off_t)-1)
    {
#if DEBUG0
        fprintf(stderr,
                "Cannot seek to start of boot file %s: %s [%d] \n",
                FILE_NAME(arg), strerror(errno), errno);
#endif
        return -1;
    }

    buf = (uint8_t *)malloc(nr_bytes);
    if (buf == NULL)
    {
#if DEBUG0
        fprintf(stderr, "Out of memory allocating input buffer\n");
#endif
        return -1;
    }
    while (done < nr_bytes)
    {
        rv = utils_safe_read(arg->fd, &buf[done], nr_bytes-done);
        if (rv < 0)
        {
            goto end;
        }
        else if (!rv)
        {
            rv = -1;
            goto end;
        }
        else
        {
            done += rv;
        }
    }
    rv = xmodem_go(ctx, buf, nr_bytes, force_128);

end:
    free(buf);
    return rv;
}

/* End file */

