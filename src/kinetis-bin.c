/* kinetis-bin.c */
/* Copyright (c) Kynesim Ltd, 2019 */

/* As for kinetis-srec.c, but expecting a .bin file instead of .s19 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>

#include "upc2/up.h"
#include "upc2/kinetis-bin.h"
#include "upc2/utils.h"

#define NAME_MAYBE_NULL(n) (((n) == NULL) ? "(no file name)" : (n))


typedef struct kcontext_struct
{
    int state;
    int pkt_state;
    int count;
    int content_len;
    off_t file_bytes; /* Number of bytes in the file */
    int file_nbytes;  /* Number of bytes in file_buffer */
    uint8_t file_buffer[32];
    uint8_t buffer[256];
} kcontext_t;


#define PKT_START 0x5a

#define PKT_TYPE_ACK       0xa1
#define PKT_TYPE_NAK       0xa2
#define PKT_TYPE_ACK_ABORT 0xa3
#define PKT_TYPE_COMMAND   0xa4
#define PKT_TYPE_DATA      0xa5
#define PKT_TYPE_PING      0xa6
#define PKT_TYPE_PING_RESP 0xa7

#define CMD_WRITE_MEMORY             0x04
#define CMD_RESET                    0x0b
#define CMD_FLASH_ERASE_ALL_UNSECURE 0x0d
#define RESP_GENERIC_RESPONSE        0xa0

#define GET_PARAMETER(p, n) \
    (p[10 + 4*n] | \
     (p[10 + 4*n + 1] << 8) | \
     (p[10 + 4*n + 2] << 16) | \
     (p[10 + 4*n + 3] << 24))

/* Overall state machine */
#define STATE_WAIT_FOR_PING_RESPONSE 0
#define STATE_WAIT_FOR_ERASE_ACK     1
#define STATE_WAIT_FOR_ERASE_RESP    2
#define STATE_WAIT_FOR_WRITE_ACK     3
#define STATE_WAIT_FOR_WRITE_RESP    4
#define STATE_WAIT_FOR_DATA_ACK      5
#define STATE_WAIT_FOR_DATA_RESP     6
#define STATE_WAIT_FOR_RESET_ACK     7
#define STATE_WAIT_FOR_RESET_RESP    8


/* Packet reader state machine */
#define PKT_WAIT_FOR_START     0
#define PKT_WAIT_FOR_TYPE      1
#define PKT_READ_PING_RESPONSE 2
#define PKT_READ_HEADER        3
#define PKT_READ_BODY          4


static uint32_t crc_byte(uint32_t crc, uint8_t byte)
{
    int i;

    crc ^= byte << 8;
    for (i = 0; i < 8; i++)
    {
        uint32_t temp = crc << 1;
        if (crc & 0x8000)
            temp ^= 0x1021;
        crc = temp;
    }
    return crc;
}


/* Slow, dumb CRC routine for the moment */
static uint16_t crc_packet(const uint8_t *buffer)
{
    uint32_t crc = 0;
    int len = (buffer[2] | (buffer[3] << 8)) + 6;
    int i;

    for (i = 0; i < 4; i++)
        crc = crc_byte(crc, buffer[i]);
    for (i = 6; i < len; i++)
        crc = crc_byte(crc, buffer[i]);

    return crc & 0xffff;
}

static uint16_t crc_split_packet(const uint8_t *header, const uint8_t *buffer)
{
    uint32_t crc = 0;
    int len = (header[2] | (header[3] << 8));
    int i;

    for (i = 0; i < 4; i++)
        crc = crc_byte(crc, header[i]);
    for (i = 0; i < len; i++)
        crc = crc_byte(crc, buffer[i]);

    return crc & 0xffff;
}


static int send_packet(up_bio_t *bio,
                       const uint8_t *buffer,
                       int nbytes)
{
    int rv;

    while (nbytes > 0)
    {
        rv = bio->safe_write(bio, buffer, nbytes);
        if (rv < 0)
            return rv;
        nbytes -= rv;
        buffer += rv;
    }
    return 0;
}


static int send_ping(up_bio_t *bio)
{
    const uint8_t buffer[2] = { PKT_START, PKT_TYPE_PING };

    return send_packet(bio, buffer, 2);
}


static int send_ack(up_bio_t *bio)
{
    const uint8_t buffer[2] = { PKT_START, PKT_TYPE_ACK };

    return send_packet(bio, buffer, 2);
}


static int send_command0(up_bio_t *bio, uint8_t command)
{
    uint8_t buffer[10];
    uint16_t crc;

    buffer[0] = PKT_START;
    buffer[1] = PKT_TYPE_COMMAND;
    buffer[2] = 0x04;
    buffer[3] = 0x00; /* Length (2 bytes) */
    /* Defer CRC */
    buffer[6] = command;
    buffer[7] = 0x00; /* Flags */
    buffer[8] = 0x00; /* Reserved */
    buffer[9] = 0x00; /* Parameter Count */

    crc = crc_packet(buffer);
    buffer[4] = crc & 0xff;
    buffer[5] = (crc >> 8) & 0xff;
    return send_packet(bio, buffer, 10);
}


static int send_command2(up_bio_t *bio,
                         uint8_t command,
                         uint32_t param1,
                         uint32_t param2)
{
    uint8_t buffer[18];
    uint16_t crc;

    buffer[0] = PKT_START;
    buffer[1] = PKT_TYPE_COMMAND;
    buffer[2] = 0x0c;
    buffer[3] = 0x00; /* Length (2 bytes) */
    /* Defer CRC */
    buffer[6] = command;
    buffer[7] = 0x00; /* Flags */
    buffer[8] = 0x00; /* Reserved */
    buffer[9] = 0x02; /* parameter Count */
    buffer[10] = param1 & 0xff;
    buffer[11] = (param1 >> 8) & 0xff;
    buffer[12] = (param1 >> 16) & 0xff;
    buffer[13] = (param1 >> 24) & 0xff;
    buffer[14] = param2 & 0xff;
    buffer[15] = (param2 >> 8) & 0xff;
    buffer[16] = (param2 >> 16) & 0xff;
    buffer[17] = (param2 >> 24) & 0xff;

    crc = crc_packet(buffer);
    buffer[4] = crc & 0xff;
    buffer[5] = (crc >> 8) & 0xff;
    return send_packet(bio, buffer, 18);
}


static int send_raw_data(up_bio_t *bio,
                         const uint8_t *buffer,
                         uint16_t nbytes)
{
    uint8_t header[6];
    uint16_t crc;
    int rv;

    if (nbytes > 32)
        nbytes = 32;
    header[0] = PKT_START;
    header[1] = PKT_TYPE_DATA;
    header[2] = nbytes;
    header[3] = 0x00;
    crc = crc_split_packet(header, buffer);
    header[4] = crc & 0xff;
    header[5] = (crc >> 8) & 0xff;
    rv = send_packet(bio, header, 6);
    if (rv == 0)
        rv = send_packet(bio, buffer, nbytes);
    if (rv < 0)
        return rv;
    return nbytes;
}


static int start_write(kcontext_t *kctx,
                       up_context_t *upc,
                       up_load_arg_t *arg)
{
    kctx->file_bytes = lseek(arg->fd, 0, SEEK_END);
    if (kctx->file_bytes == (off_t)-1)
    {
        fprintf(stderr, "Cannot lseek() %s: %s [%d]\n",
                NAME_MAYBE_NULL(arg->file_name),
                strerror(errno),
                errno);
        return -1;
    }
    lseek(arg->fd, 0, SEEK_SET);

    /* Now send the write command */
    if (send_command2(upc->bio,
                      CMD_WRITE_MEMORY,
                      arg->offset,
                      kctx->file_bytes) < 0)
    {
        fprintf(stderr,
                "\n**Error %d sending WRITE command: %s\n",
                errno, strerror(errno));
        return -1;
    }
    return 0;
}


static int handle_generic_response(kcontext_t *kctx,
                                   up_context_t *upc,
                                   up_load_arg_t *arg)
{
    uint32_t status = GET_PARAMETER(kctx->buffer, 0);
    uint32_t tag = GET_PARAMETER(kctx->buffer, 1);
    int rv;

    switch (kctx->state)
    {
        case STATE_WAIT_FOR_ERASE_RESP:
            if (tag != CMD_FLASH_ERASE_ALL_UNSECURE)
            {
                fprintf(stderr,
                        "\n**Error: unexpected tag 0x%02x in response\n",
                        tag);
                return -1;
            }
            if (status != 0)
            {
                fprintf(stderr, "\n**Error %d erasing flash\n", status);
                return -1;
            }
            fprintf(stderr, "...done\n");
            if (send_ack(upc->bio) < 0)
            {
                fprintf(stderr,
                        "**Error %d sending ACK: %s\n",
                        errno, strerror(errno));
                return -1;
            }
            fprintf(stderr, "Writing...\n");
            if ((rv = start_write(kctx, upc, arg)) < 0)
                return -1;
            kctx->state = STATE_WAIT_FOR_WRITE_ACK;
            break;

        case STATE_WAIT_FOR_WRITE_RESP:
            if (tag != CMD_WRITE_MEMORY)
            {
                fprintf(stderr,
                        "\n**Error: unexpected tag 0x%02x in response\n",
                        tag);
                return -1;
            }
            if (status != 0)
            {
                fprintf(stderr, "\n**Error %d writing flash\n", status);
                return -1;
            }
            if (send_ack(upc->bio) < 0)
            {
                fprintf(stderr,
                        "**Error %d sending ACK: %s\n",
                        errno, strerror(errno));
                return -1;
            }
            kctx->file_nbytes = (kctx->file_bytes > 32) ?
                32 : kctx->file_bytes;
            rv = utils_safe_read(arg->fd,
                                 kctx->file_buffer,
                                 kctx->file_nbytes);
            if (rv < 0)
            {
                fprintf(stderr,
                        "Error reading bin file %s: %s [%d]\n",
                        NAME_MAYBE_NULL(arg->file_name),
                        strerror(errno),
                        errno);
                return -1;
            }
            if (send_raw_data(upc->bio, kctx->file_buffer, rv) < 0)
            {
                fprintf(stderr,
                        "**Error %d sending data: %s\n",
                        errno, strerror(errno));
                return -1;
            }
            /* Stash in case we need to re-send */
            kctx->file_nbytes = rv;
            kctx->file_bytes -= rv;
            kctx->state = STATE_WAIT_FOR_DATA_ACK;
            break;

        case STATE_WAIT_FOR_DATA_RESP:
            if (tag != CMD_WRITE_MEMORY)
            {
                fprintf(stderr,
                        "\n**Error: unexpected tag 0x%02x in response\n",
                        tag);
                return -1;
            }
            if (status != 0)
            {
                fprintf(stderr, "\n**Error %d writing flash\n", status);
                return -1;
            }
            if (send_ack(upc->bio) < 0)
            {
                fprintf(stderr,
                        "**Error %d sending ACK: %s\n",
                        errno, strerror(errno));
                return -1;
            }
            fprintf(stderr, "Download complete\nResetting chip...");
            if (send_command0(upc->bio, CMD_RESET) < 0)
            {
                fprintf(stderr,
                        "\n**Error %d sending RESET command: %s\n",
                        errno,
                        strerror(errno));
                return -1;
            }
            kctx->state = STATE_WAIT_FOR_RESET_ACK;
            break;

        case STATE_WAIT_FOR_RESET_RESP:
            if (tag != CMD_RESET)
            {
                fprintf(stderr,
                        "\n**Error: unexpected tag 0x%02x in response\n",
                        tag);
                return -1;
            }
            if (status != 0)
            {
                fprintf(stderr, "\n**Error %d in reset\n", status);
                return -1;
            }
            if (send_ack(upc->bio) < 0)
            {
                fprintf(stderr,
                        "**Error %d sending ACK: %s\n",
                        errno, strerror(errno));
                return -1;
            }
            return 1;

        default:
            /* We have no idea what this is about */
            break;
    }

    return 0;
}


/* Reads the buffer, looking for a Kinetis packet.  Returns -1 if no
 * (complete) packet is found in the buffer, otherwise the index of
 * the last byte of the packet.
 */
static int read_packet(kcontext_t *kctx,
                       const uint8_t *buf,
                       int rv)
{
    int i;

    for (i = 0; i < rv; i++)
    {
        switch (kctx->pkt_state)
        {
            case PKT_WAIT_FOR_START:
                if (buf[i] == PKT_START)
                {
                    kctx->count = 1;
                    kctx->buffer[0] = PKT_START;
                    kctx->pkt_state = PKT_WAIT_FOR_TYPE;
                }
                break;

            case PKT_WAIT_FOR_TYPE:
                kctx->count = 2;
                kctx->buffer[1] = buf[i];
                switch (buf[i])
                {
                    case PKT_TYPE_ACK:
                    case PKT_TYPE_NAK:
                    case PKT_TYPE_ACK_ABORT:
                    case PKT_TYPE_PING:
                        kctx->pkt_state = PKT_WAIT_FOR_START;
                        kctx->count = 0;
                        return i;

                    case PKT_TYPE_PING_RESP:
                        kctx->pkt_state = PKT_READ_PING_RESPONSE;
                        break;

                    case PKT_TYPE_COMMAND:
                    case PKT_TYPE_DATA:
                        kctx->pkt_state = PKT_READ_HEADER;
                        break;

                    default:
                        /* Invalid, assume we mis-identified a START */
                        kctx->pkt_state = PKT_WAIT_FOR_START;
                        kctx->count = 0;
                        break;
                }
                break;

            case PKT_READ_PING_RESPONSE:
                kctx->buffer[kctx->count++] = buf[i];
                if (kctx->count == 10)
                {
                    kctx->pkt_state = PKT_WAIT_FOR_START;
                    kctx->count = 0;
                    return i;
                }
                break;

            case PKT_READ_HEADER:
                kctx->buffer[kctx->count++] = buf[i];
                if (kctx->count == 6)
                {
                    kctx->content_len =
                        kctx->buffer[2] | (kctx->buffer[3] << 8);
                    if (kctx->content_len > 250)
                    {
                        /* Too much, assume a bad START again */
                        kctx->pkt_state = PKT_WAIT_FOR_START;
                        kctx->count = 0;
                        break;
                    }
                    kctx->pkt_state = PKT_READ_BODY;
                }
                break;

            case PKT_READ_BODY:
                kctx->buffer[kctx->count++] = buf[i];
                if (kctx->count == kctx->content_len + 6)
                {
                    kctx->pkt_state = PKT_WAIT_FOR_START;
                    kctx->count = 0;
                    return i;
                }
                break;
        }
    }

    return -1;
}



static void *init_kinetis(void)
{
    return malloc(sizeof(kcontext_t));
}


static int shutdown_kinetis(void *h, up_context_t *ctx)
{
    free(h);
    return 0;
}


static int prepare_kinetis(void *h, up_context_t *upc, up_load_arg_t *arg)
{
    int rv;
    kcontext_t *kctx = (kcontext_t *)h;

    kctx->state = STATE_WAIT_FOR_PING_RESPONSE;
    kctx->pkt_state = PKT_WAIT_FOR_START;
    kctx->count = 0;
    kctx->file_bytes = 0;
    arg->echo = 0;

    /* Initialise h */
    rv = utils_protocol_set_baud(h, upc, arg);
    if (rv < 0)
        return rv;
    return send_ping(upc->bio);
}


static int maybe_kinetis_bootload(void          *h,
                                  up_context_t  *upc,
                                  up_load_arg_t *arg,
                                  const uint8_t *buf,
                                  int           rv)
{
    kcontext_t *kctx = (kcontext_t *)h;
    int last;

    while (rv > 0)
    {
        last = read_packet(kctx, buf, rv);
        if (last == -1)
        {
            /* Didn't find anything */
            /* XXX: resend? */
            return 0;
        }

        buf += ++last;
        rv -= last;

        switch (kctx->buffer[1])
        {
            case PKT_TYPE_PING:
                /* We never expect to get one of these */
                fprintf(stderr, "Ping\n");
                break;

            case PKT_TYPE_PING_RESP:
                fprintf(stderr,
                        "Ping Response, protocol %c %d.%d.%d, options 0x%04x\n",
                        kctx->buffer[5],
                        kctx->buffer[4],
                        kctx->buffer[3],
                        kctx->buffer[2],
                        kctx->buffer[6] | (kctx->buffer[7] << 8));
                if (kctx->state == STATE_WAIT_FOR_PING_RESPONSE)
                {
                    if (kctx->buffer[5] != 'P')
                    {
                        fprintf(stderr, "**Error: not in bootloader\n");
                        return -1;
                    }

                    /* We have the attention of the bootloader and it
                     * has correctly autobauded.  The next step is to
                     * erase the flash.
                     */
                    fprintf(stderr, "Erasing...");
                    fflush(stderr);
                    if (send_command0(upc->bio,
                                      CMD_FLASH_ERASE_ALL_UNSECURE) < 0)
                    {
                        fprintf(stderr,
                                "**Error %d sending ERASE command: %s\n",
                                errno, strerror(errno));
                        return -1;
                    }
                    kctx->state = STATE_WAIT_FOR_ERASE_ACK;
                }
                /* Else we weren't expecting this and don't care */
                break;

            case PKT_TYPE_ACK:
                switch (kctx->state)
                {
                    case STATE_WAIT_FOR_ERASE_ACK:
                    case STATE_WAIT_FOR_RESET_ACK:
                        fputc('\n', stderr);
                        /* FALL THROUGH */
                    case STATE_WAIT_FOR_WRITE_ACK:
                        kctx->state++;
                        break;

                    case STATE_WAIT_FOR_DATA_ACK:
                        /* Are we done, or is there more data? */
                        if (kctx->file_bytes == 0)
                        {
                            /* All done, a response will be coming */
                            kctx->state = STATE_WAIT_FOR_DATA_RESP;
                            break;
                        }
                        /* Otherwise send some more data */
                        {
                            int rv;

                            kctx->file_nbytes = (kctx->file_bytes > 32) ?
                                32 : kctx->file_bytes;
                            rv = utils_safe_read(arg->fd,
                                                 kctx->file_buffer,
                                                 kctx->file_nbytes);
                            if (rv < 0)
                            {
                                fprintf(stderr,
                                        "Error reading bin file %s: %s [%d]\n",
                                        NAME_MAYBE_NULL(arg->file_name),
                                        strerror(errno),
                                        errno);
                                return -1;
                            }
                            if (send_raw_data(upc->bio,
                                              kctx->file_buffer,
                                              rv) < 0)
                            {
                                fprintf(stderr,
                                        "**Error %d sending data: %s\n",
                                        errno, strerror(errno));
                                return -1;
                            }
                            /* Stash in case we need to re-send */
                            kctx->file_nbytes = rv;
                            kctx->file_bytes -= rv;
                        }
                        break;

                    default:
                        /* No idea what this is an ACK for */
                        break;
                }
                break;

            case PKT_TYPE_NAK:
                /* Somehow what we sent got mangled.  Try again */
                switch (kctx->state)
                {
                    case STATE_WAIT_FOR_ERASE_ACK:
                        fprintf(stderr, " (retry)");
                        fflush(stderr);
                        if (send_command0(upc->bio,
                                          CMD_FLASH_ERASE_ALL_UNSECURE) < 0)
                        {
                            fprintf(stderr,
                                    "**Error %d sending ERASE command: %s\n",
                                    errno, strerror(errno));
                            return -1;
                        }
                        break;

                    case STATE_WAIT_FOR_WRITE_ACK:
                        fprintf(stderr, " (retry)");
                        fflush(stderr);
                        if (send_command2(upc->bio,
                                          CMD_WRITE_MEMORY,
                                          0,
                                          kctx->file_nbytes) < 0)
                        {
                            fprintf(stderr,
                                    "**Error %d sending WRITE command: %s\n",
                                    errno, strerror(errno));
                            return -1;
                        }
                        break;

                    case STATE_WAIT_FOR_RESET_ACK:
                        fprintf(stderr, " (retry)");
                        fflush(stderr);
                        if (send_command0(upc->bio, CMD_RESET) < 0)
                        {
                            fprintf(stderr,
                                    "**Error %d sending RESET command: %s\n",
                                    errno, strerror(errno));
                            return -1;
                        }
                        break;

                    case STATE_WAIT_FOR_DATA_ACK:
                        if (send_raw_data(upc->bio,
                                          kctx->file_buffer,
                                          kctx->file_nbytes) < 0)
                        {
                            fprintf(stderr,
                                    "**Error %d sending data: %s\n",
                                    errno, strerror(errno));
                            return -1;
                        }
                        break;

                    default:
                        /* No idea what this refers to */
                        break;
                }
                break;

            case PKT_TYPE_ACK_ABORT:
                fprintf(stderr, "\n**Boot Aborted\n");
                return -1;

            case PKT_TYPE_COMMAND:
            {
                /* First validate the response */
                uint16_t crc = crc_packet(kctx->buffer);
                int result;

                if ((crc & 0xff) != kctx->buffer[4] ||
                    ((crc >> 8) & 0xff) != kctx->buffer[5])
                {
                    fprintf(stderr, "\n**Invalid CRC received, aborting\n");
                    return -1;
                }

                /* What's the command? */
                if (kctx->buffer[6] != RESP_GENERIC_RESPONSE)
                {
                    fprintf(stderr,
                            "\n**Unexpected command/response 0x%02x\n",
                            kctx->buffer[6]);
                    /* Otherwise ignore it */
                    break;
                }
                result = handle_generic_response(kctx, upc, arg);
                if (result < 0)
                    return -1;
                if (result > 0)
                    return 1;
                break;
            }

            case PKT_TYPE_DATA:
                fprintf(stderr, "\n**Ignoring unexpected data\n");
                break;
        }
    }

    /* Still working on it */
    return 0;
}


const up_protocol_t kinetis_bin_protocol =
{
    "kinetis",
    init_kinetis,
    prepare_kinetis,
    maybe_kinetis_bootload,
    NULL,
    shutdown_kinetis
};

