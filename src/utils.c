/* utils.c */
/* Copyright (c) Kynesim Ltd, 2012-2015 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <poll.h>

#include "upc2/utils.h"
#include "upc2/up.h"


int utils_safe_write(int fd, const uint8_t *data, int len) {
    int done = 0;
    int rv;

    while (done < len) {
        rv = write(fd, &data[done], len - done);
        if (rv < 0) {
            if (errno == EINTR || errno == EAGAIN)
                continue;
            fprintf(stderr, "! Cannot write(fd=%d): %s [%d]\n",
                    fd, strerror(errno), errno);
            return -1;
        }
        done += rv;
    }
    return done;
}


int utils_safe_read(int fd, uint8_t *data, int len)
{
    int rv;

    while (1)
    {
        rv = read(fd, data, len);
        if (rv < 0)
        {
            if (!(errno == EINTR || errno == EAGAIN))
                return rv;
        }
        else
        {
            /* NB: can be zero */
            return rv;
        }
    }
    return -1;
}


int utils_check_critical_control(up_context_t *ctx) {
    int rv;
    char bx;

    rv = read(ctx->ttyfd, &bx, 1);
    if (rv == 1)
    {
        if (!ctx->control_mode && bx == 0x01)
        {
            ctx->control_mode = 1;
            return 0;
        }
        if (ctx->control_mode)
        {
            switch (bx)
            {
                case 'x':
                    fprintf(stderr, "Detected C-a C-x ; dying.\n");
                    return -2;
                default:
                    // No one cares in grouch.
                    break;
            }
            ctx->control_mode = 0;
        }
    }
    return 0;
}

int utils_bio_safe_write(up_bio_t *bio, const uint8_t *data, int nr) {
    int done = 0;
    struct pollfd fds[1];

    while (done < nr) {
        int rv;

        rv = bio->write(bio, &data[done], nr-done);
        if (rv < 0) {
            if (errno != EINTR && errno != EAGAIN) {
                fprintf(stderr, "! Cannot write to bio:  %s [%d] \n",
                        strerror(errno), errno);
                exit(1);
            }
        } else {
            done += rv;
        }
        if (done < nr) {
            fds[0].revents = 0;
            fds[0].events = POLLOUT;
            fds[0].fd = bio->poll_fd(bio);
            poll(fds, 1, 1000);
        }
    }
    return done;
}


int utils_safe_printf(up_context_t *ctx, const char *str, ...)
{
    va_list ap;
    char buf[4096];
    int l;

    va_start(ap, str);
    l = vsnprintf(buf, 4096, str, ap);
    va_end(ap);
    utils_safe_write(ctx->ttyfd, (const uint8_t *)buf, l);
    return l;
}


/* End file */
