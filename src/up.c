/* up.c */
/* (C) Kynesim Ltd 2012-15 */

/** @file
 *
 *  Does most of the heavy lifting for upc2
 *
 * @author Richard Watts <rrw@kynesim.co.uk>
 * @date   2015-10-08
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>

#include "upc2/up.h"
#include "upc2/utils.h"


static void groan_with(up_context_t *ctx, int which);
static void console_help(up_context_t *upc);

static void console_help(up_context_t *upc) {
    utils_safe_printf(upc,
                      "\n"
                      "upc2 0.1 (C) Kynesim Ltd 2012-5\n"
                      "\n"
                      "Console help\n"
                      "\n"
                      "C-a h                This help message\n"
                      "C-a x                Quit.\n"
                      "C-a C-a              Literal C-a \n"
                      "C-a <anything else>  Spiders?\n"
                      "\n");
}

static void groan_with(up_context_t *upc, int which) {
    static const char * groans[] =
    {
        "Did your mother not warn you about strange escape codes?\n",
        "War never changes\n",
        "You are in a maze of twisty IPv6 addresses, all the same\n",
        "The only way to win is not to invoke escape codes at random\n",
        "Right on, Commander!\n"
    };
    int p = which % (sizeof(groans)/sizeof(char *));
    utils_safe_printf(upc, groans[p]);
}


int up_create(up_context_t **ctxp) {
    up_context_t *ctx = NULL;
    int rv = 0;

    ctx = (up_context_t *)malloc(sizeof(up_context_t));
    memset(ctx, '\0', sizeof(up_context_t));
    ctx->logfd = ctx->ttyfd = -1;
    (*ctxp) = ctx;
    if (rv < 0) {
        up_dispose(ctxp);
    }
    return rv;
}

int up_dispose(up_context_t **ctxp) {
    if (!ctxp || !*ctxp) return 0;
    {
        up_context_t *ctx = *ctxp;
        if (ctx->bio) { ctx->bio->dispose(ctx->bio); ctx->bio = NULL; }
        if (ctx->logfd >= 0) { close(ctx->logfd); ctx->logfd = -1; }
        free(ctx); (*ctxp) = NULL;
    }
    return 0;
}

int up_attach_bio(up_context_t *ctx, up_bio_t *bio) {
    if (ctx->bio) { ctx->bio->dispose(ctx->bio); }
    ctx->bio = bio;
    return 0;
}

int up_start_console(up_context_t *ctx, int tty_fd) {
    struct termios t;
    tcgetattr(tty_fd, &t);
    ctx->tc = t;
    ctx->control_mode = 0;
    ctx->cur_arg = 0;
    cfmakeraw(&t);
    /* Don't generate SIGINT - it is normal input for our slave device */
    t.c_lflag &= ~ISIG;
    /* \n is \r\n else I will go quietly insane
     */
    t.c_oflag |= OPOST;
    tcsetattr(tty_fd, TCSANOW, &t);
    ctx->ttyfd = tty_fd;
    ctx->ttyflags = fcntl(ctx->ttyfd, F_GETFL, 0);
    fcntl(ctx->ttyfd, F_SETFL, O_NONBLOCK);
    return utils_safe_printf(ctx,
                             "upc2: Starting terminal. C-a h for help\n");
}


int up_operate_console(up_context_t  *ctx,
                       up_load_arg_t *args,
                       int            nr_args) {
    uint8_t buf[256];
    int rv;
    int ret = 0;
    struct pollfd fds[2];
    up_load_arg_t *cur_arg = &args[ctx->cur_arg];

    fds[0].revents = fds[1].revents = 0;
    fds[0].fd = ctx->bio->poll_fd(ctx->bio);
    fds[0].events = POLLIN |POLLERR;
    fds[1].fd = ctx->ttyfd;
    fds[1].events = POLLIN | POLLERR;

    // Tick around every 1s or so. Our writes are all synchronous,
    // so we don't care about POLLOUT.
    poll(fds, 2, 1000);
    if ((fds[0].revents & (POLLHUP | POLLERR)) ||
        (fds[1].revents & (POLLHUP | POLLERR))) {
        utils_safe_printf(ctx,
                          "! upc2 I/O falied: fd %d / 0x%04x , %d 0x%04x\n",
                          fds[0].fd, fds[0].revents,
                          fds[1].fd, fds[1].revents);
        ret = -1;
        goto end;
    }

    /* Read from serial, copy to output and (potentially) log */
    rv = ctx->bio->read( ctx->bio, buf, 32 );
    if (rv > 0) {
        utils_safe_write(ctx->ttyfd, buf, rv);
        if (ctx->logfd >= 0) {
            utils_safe_write(ctx->logfd, buf, rv);
        }
    }

    /* Run protocol state machines */
    if (ctx->cur_arg < nr_args) {
        if (cur_arg->fd > -1) {
            // Run the state machine.
            ret = cur_arg->protocol->transfer(cur_arg->protocol_handle,
                                              ctx, cur_arg,
                                              buf, rv);

            // If ret < 0, something went wrong
            if (ret < 0)
                goto end;
            // If ret > 0, we've terminated. Move to the next argument and
            // set baud.
            if (ret > 0) {
                if (cur_arg->protocol->complete != NULL)
                    cur_arg->protocol->complete(cur_arg->protocol_handle,
                                                ctx, cur_arg);
                ++ctx->cur_arg;
                cur_arg = &args[ctx->cur_arg];
                if (ctx->cur_arg < nr_args) {
                    if (cur_arg->protocol->prepare != NULL)
                        cur_arg->protocol->prepare(cur_arg->protocol_handle,
                                                   ctx, cur_arg);
                }
            }
        }
    }

    // Anything from the terminal?
    /* NB: unlike utils_check_critical_control(), this passes data
     * from the terminal to the serial output.
     */
    rv = read(ctx->ttyfd, buf, 32);
    if (rv > 0) {
        int i, optr = 0;

        for (i = 0; i < rv; ++i) {
            if (ctx->control_mode) {
                static int groan = 0;

                switch (buf[i]) {
                    case 'h':
                        console_help(ctx);
                        break;
                    case 's':
                        utils_safe_printf(ctx, "Oh no! Spiders!\n");
                        break;
                    case 'g':
                        groan_with(ctx, groan++);
                        break;
                    case 'x':
                        ret = -1;
                        break;
                default:
                    // Literal whatever-it-is.
                    buf[optr++] = buf[i];
                }
                ctx->control_mode = 0;
            } else if (buf[i] == 0x01) { // C-a
                ctx->control_mode = 1;
            } else {
                buf[optr++] = buf[i];
            }
        }
        /** @todo Don't echo while downloads are ongoing? */
        ctx->bio->write(ctx->bio, buf, optr);
    } else if (rv == 0) {
        utils_safe_printf(ctx, "! upc2: Input closed.\n");
        ret = -1;
    } else if (rv < 0) {
        if (!(errno == EINTR || errno == EAGAIN))  {
            utils_safe_printf(ctx,
                              "! upc2: Failed to read tty:  %s [%d]\n",
                              strerror(errno), errno);
            ret = -1;
        }
    }
end:
    return ret;
}

int up_finish_console(up_context_t *ctx) {
    utils_safe_printf(ctx, "! upc2: Terminating console.\n");
    tcsetattr(ctx->ttyfd, TCSANOW, &ctx->tc);
    fcntl(ctx->ttyfd, F_SETFL, ctx->ttyflags);
    return 0;
}



int up_become_console(up_context_t *ctx, up_load_arg_t *args, int nr_args) {
    int rv, r2;
    rv = up_start_console(ctx, STDIN_FILENO);
    if (rv < 0)
    {
        fprintf(stderr, "up_start_console returned %d\n", rv);
        return rv;
    }
    /* Prep the first protocol handler */
    if (args[0].protocol->prepare != NULL)
        args[0].protocol->prepare(args[0].protocol_handle, ctx, &args[0]);
    do {
        rv = up_operate_console(ctx, args, nr_args);
    } while (rv >= 0);
    /* Wind down the last protocol handler */
    if (ctx->cur_arg < nr_args &&
        args[ctx->cur_arg].protocol->complete != NULL)
    {
        args[ctx->cur_arg].protocol->complete(
            args[ctx->cur_arg].protocol_handle,
            ctx,
            &args[ctx->cur_arg]);
    }
    r2 = up_finish_console(ctx);
    if (r2 < 0)
        return r2;
    return rv;
}

int up_set_log_fd(up_context_t *ctx, const int fd) {
    ctx->logfd = fd;
    return 0;
}

int up_read_baud(const char *lne)
{
    char *p = NULL;
    int baud = strtoul(lne, &p, 0);
    if (p)
    {
        if (*p == 'm')
        {
            baud = baud * 1000000;
        }
        else if (*p == 'k')
        {
            baud = baud * 1000;
        }
        else if (*p)
        {
            return -1;
        }
    }
    return baud;
}

/* End file */

