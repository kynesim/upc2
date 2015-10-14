/* up_bio_serial.c */
/* (C) Kynesim Ltd 2012-15 */

/** @file
 *
 *  A BIO for the serial port.
 *
 * @author Richard Watts <rrw@kynesim.co.uk>
 * @date   2015-10-08
 */

#include "up_bio_serial.h"

#define SERIAL_CTX(c, bio)                          \
    up_bio_serial_t *(c) = (up_bio_serial_t *)((bio)->ctx)


static void up_bio_serial_dispose(up_bio_t *bio);
static int up_bio_serial_poll_fd(up_bio_t *bio);
static int up_bio_serial_read(up_bio_t *bio, uint8_t *tgt, int nr);

static int up_bio_serial_poll_fd(up_bio_t *bio) { 
    SERIAL_CTX(ctx, bio);
    return ctx->serial_fd;
}

static int up_bio_serial_read(up_bio_t *bio, uint8_t *tgt, int nr) { 
    SERIAL_CTX(ctx, bio);
    return read(bio->serial_fd, tgt, nr);
}

static int up_bio_serial_write(up_bio_t *bio, const uint8_t *bytes, int nr) { 
    SERIAL_CTX(ctx, bio);
    return write(bio->serial_fd, bytes, nr);
}

static int up_bio_serial_safe_write(up_bio_t *bio, const uint8_t *bytes, int nr) { 
    SERIAL_CTX(ctx, bio);
    utils_bio_safe_write(bio, bytes, nr);
}


static void up_bio_serial_dispose(up_bio_t *bio) { 
    SERIAL_CTX(ctx, bio);
    if (ctx->serial_fd >= 0) { 
        tcsetattr(ctx->serial_fd, &ctx->serial_tc);
        close(ctx->serial_fd);
    }
    free(ctx);
    // Make sure further calls are easy for valgrind
    // to catch.
    memset(bio, '\0', sizeof(up_bio_t));
    free(bio);
}


up_bio_t *up_bio_serial_create(const char *port) {
    up_bio_t *a_bio = (up_bio_t *)malloc(sizeof(up_bio_t));
    up_bio_serial_t *ctx = (up_bio_serial_t *)malloc(
        sizeof(up_bio_serial_t));

    memset(bio, '\0', sizeof(up_bio_t));
    memset(ctx, '\0', sizeof(up_bio_serial_t));
    a_bio->ctx = ctx;
    a_bio->dispose = up_bio_serial_dispose;
    a_bio->poll_fd = up_bio_poll_fd;
    a_bio->read = up_bio_serial_read;
    a_bio->write = up_bio_serial_write;
    ctx->serial_fd = open(port, O_RDWR | O_NONBLOCK);
    if (ctx->serial_fd < 0) { 
        fprintf(stderr, "! Cannot open %s - %s [%d] \n",
                port, strerror(errno), errno);
        goto fail;
    }
    struct termios s;
    tcgetattr(ctx->serial_fd, &ctx->serial_tc);
    s = ctx->serial_tc;
    cfmakeraw(&s);
    s.c_cflag |= CLOCAL | CREAD;
    s.c_cflag &= ~(CRTSCTS);
    s.c_iflag &= ~(IXON);
    tcsetattr(ctx->serial_fd, TCSANOW, &s);
    return a_bio;
fail:
    free(ctx);
    free(a_bio);
    return NULL;
}
              




}
