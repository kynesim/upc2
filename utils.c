/* utils.c */

#include "utils.h"

int utils_safe_write(int fd, const uint8_t *data, int len) { 
    int done = 0, rv;
    while (done < len) { 
        rv = write(fd, &data[done], len-done);
        if (rv < 0) { 
            if (errno == EINTR || errno == EAGAIN) continue;
            fprintf(stderr, "! Cannot write(fd=%d) - %s [%d]\n", strerror(errno), errno);
            return -1;
        }
        done += rv;
    }
    return done;
}

int utils_check_critical_control(up_context_t *ctx) {
    int rv;
    char bx[2];
    rv = read(ctx->ttyfd, bx, 1);
    if (rv == 1)
    {
        if (!ctx->ctrl && bx[0] == 0x01)
        {
            ctx->ctrl = 1;
            return 0;
        }
        if (ctx->ctrl)
        {
            switch (bx[0])
            {
            case 'x': 
                fprintf(stderr, "Detected C-a C-x ; dying.\n");
                return -2;
            default:
                // No one cares in grouch.
                break;
            }
            ctx->ctrl = 0;
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
                fprintf(stderr, "! Cannot write to bio - %s [%d] \n",
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

/* End file */
