/* up_bio_serial.c */
/* (C) Kynesim Ltd 2012-15 */

/** @file
 *
 *  A BIO for the serial port.
 *
 * @author Richard Watts <rrw@kynesim.co.uk>
 * @date   2015-10-08
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <termios.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "upc2/up.h"
#include "upc2/up_bio_serial.h"
#include "upc2/utils.h"


#define SERIAL_HANDLE(c, bio)                          \
    up_bio_serial_t *(c) = (up_bio_serial_t *)((bio)->handle)


static void up_bio_serial_dispose(up_bio_t *bio);
static int up_bio_serial_poll_fd(up_bio_t *bio);
static int up_bio_serial_read(up_bio_t *bio, uint8_t *tgt, int nr);

static int up_bio_serial_poll_fd(up_bio_t *bio) {
    SERIAL_HANDLE(handle, bio);
    return handle->serial_fd;
}

static int up_bio_serial_read(up_bio_t *bio, uint8_t *tgt, int nr) {
    SERIAL_HANDLE(handle, bio);
    return read(handle->serial_fd, tgt, nr);
}

static int up_bio_serial_write(up_bio_t      *bio,
                               const uint8_t *bytes,
                               int            nr) {
    SERIAL_HANDLE(handle, bio);
    return write(handle->serial_fd, bytes, nr);
}

static int up_bio_serial_set_baud(up_bio_t *bio, int baud, int flow_control) {
    SERIAL_HANDLE(handle, bio);

    if (baud || handle->last_flow_control != flow_control)
    {
        struct termios tios;

        printf("[[ Changing baud rate to %d / %s ]]\n", baud,
               utils_decode_flow_control(flow_control) );
        sleep(1); /* Allow drainage */

        tcgetattr(handle->serial_fd, &tios);
        switch (flow_control) { 
        case UP_FLOW_CONTROL_NONE:
            tios.c_cflag &= ~(CRTSCTS);
            tios.c_iflag &= ~(IXON);
            break;
        case UP_FLOW_CONTROL_RTSCTS:
            tios.c_cflag |= (CRTSCTS);
            tios.c_iflag &= ~(IXON);
            break;
        default:
            /* Do nothing */
            break;
        }

        handle->last_flow_control = flow_control;
        cfsetspeed(&tios, baud);
        tcsetattr(handle->serial_fd, TCSADRAIN, &tios);
    }
    return 0;
}

static int up_bio_serial_safe_write(up_bio_t      *bio,
                                    const uint8_t *bytes,
                                    int            nr) {
    //SERIAL_HANDLE(handle, bio);
    return utils_bio_safe_write(bio, bytes, nr);
}

static void up_bio_serial_dispose(up_bio_t *bio) {
    SERIAL_HANDLE(handle, bio);
    if (handle->serial_fd >= 0) {
        tcsetattr(handle->serial_fd, TCSAFLUSH, &handle->serial_tc);
        close(handle->serial_fd);
    }
    free(handle);
    // Make sure further calls are easy for valgrind
    // to catch.
    memset(bio, '\0', sizeof(up_bio_t));
    free(bio);
}


up_bio_t *up_bio_serial_create(const char *port) {
    up_bio_t *a_bio = (up_bio_t *)malloc(sizeof(up_bio_t));
    up_bio_serial_t *handle =
        (up_bio_serial_t *)malloc(sizeof(up_bio_serial_t));

    memset(a_bio, '\0', sizeof(up_bio_t));
    memset(handle, '\0', sizeof(up_bio_serial_t));
    a_bio->handle = handle;
    a_bio->dispose = up_bio_serial_dispose;
    a_bio->poll_fd = up_bio_serial_poll_fd;
    a_bio->read = up_bio_serial_read;
    a_bio->write = up_bio_serial_write;
    a_bio->safe_write = up_bio_serial_safe_write;
    a_bio->set_baud = up_bio_serial_set_baud;
    handle->serial_fd = open(port, O_RDWR | O_NONBLOCK);
    if (handle->serial_fd < 0) {
        fprintf(stderr, "! Cannot open %s: %s [%d] \n",
                port, strerror(errno), errno);
        goto fail;
    }
    struct termios s;
    tcgetattr(handle->serial_fd, &handle->serial_tc);
    s = handle->serial_tc;
    cfmakeraw(&s);
    s.c_cflag |= CLOCAL | CREAD;
    s.c_cflag &= ~(CRTSCTS);
    s.c_iflag &= ~(IXON);
    handle->last_flow_control = UP_FLOW_CONTROL_NONE;
    tcsetattr(handle->serial_fd, TCSANOW, &s);
    return a_bio;
fail:
    free(handle);
    free(a_bio);
    return NULL;
}
