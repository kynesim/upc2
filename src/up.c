/* up.c */
/* (C) Kynesim Ltd 2012-15 */

/** @file
 *
 *  Does most of the heavy lifting for upc2 
 *
 * @author Richard Watts <rrw@kynesim.co.uk>
 * @date   2015-10-08
 */

#include "up.h"
#include <stdarg.h>

static int safe_putty(up_context_t *ctx, const char *str, ... );
static void groan_with(up_context_t *upc, int which);
static int up_internal_set_baud(up_context_t *upc, up_load_arg_t *arg);
static void console_help(up_context_t *upc);

static void console_help(up_context_t *upc) { 
    safe_putty(upc, 
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

static int up_internal_set_baud(up_context_t *ctx, up_load_arg_t *arg) { 
    if (arg->baud) { 
        printf("[[ Changing baud rate to %d ]] \n", arg->baud);
        ctx->bio->set_baud(ctx->bio, arg->baud);
        
        

}

static void groan_with(up_context_t *upc, int which) {
    static const char * groans[] = 
    { 
        "Did your mother not warn you about strange escape codes?",
        "War never changes",
        "You are in a maze of twisty IPv6 addresses, all the same",
        "The only way to win is not to invoke escape codes at random",
        "Right on, Commander!"
    };
    int p = which % (sizeof(groans)/sizeof(char *));
    safe_putty(upc, groans[p]);
}


static int safe_putty(up_context_t *ctx, const char *str, ... ) 
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
    ctx->ctrl = 0;
    ctx->cur_arg = 0;
    ctx->grouchfsm = -2;
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
    safe_putty(ctx, "upc2: Starting terminal. C-a h for help\n");
}


int up_operate_console(up_context_t *ctx, up_load_arg_t *args, int nr_args) { 
    uint8_t buf[256];
    int rv;
    int ret = 0;
    struct pollfd fds[2];

    fds[0].revents = fds[1]= revents = 0;
    fds[0].fd = ctx->bio->poll_fd(ctx->bio);
    fds[0].events = POLLIN |POLLERR;
    fds[1].fd = ctx->ttyfd;
    fds[1].events = POLLIN | POLLERR;

    // Tick around every 1s or so. Our writes are all synchronous,
    // so we don't care about POLLOUT.
    poll(fds, 2, 1000);
    if ((fds[0].revents & (POLLHUP | POLLERR)) ||
        (fds[1].revents & (POLLHIP | POLLERR))) { 
        safe_putty(ctx, "! upc2 I/O falied - fd %d / 0x%04x , %d 0x%04x \n",
                   fds[0].fd, fds[0].revents,
                   fds[1].fd, fds[1].revents);
        ret = -1;
        goto end;
    }
    rv = ctx->bio->read( ctx->bio, buf, 32 );
    if (ctx->logfd >= 0) { 
        utils_safe_write(ctx->logfd, buf, rv);
    }
    if (ctx->cur_args < nr_args) { 
        if (ctx->grouchfsm == -2) { 
            // Just starting. Switch baud.
            set_baud(ctx, &args[ctx->cur_arg]);
            ctx->grouchfsm = 0;
        }
        else if (args[ctx->cur_arg.fd] > -1) { 
            // Run the state machine.
            ret = 0;
            switch (args[ctx->cur_arg].protocol) { 
            case UP_PROTOCOL_GROUCH:
                ret = maybe_grouch(ctx, &args[ctx->cur_arg], buf, rv);
                break;
            case UP_PROTOCOL_XMODEM:
                ret = xmodem_boot(ctx, &args[ctx->cur_arg]);
                break;
            default:
                // Nothing.
                break;
            }
            // If ret < 0, we have more work to do.
            if (ret < 0) { goto end; }
            // If ret > 0, we've terminated. Move to the next argument and
            // set baud.
            if (ret > 0) { 
                ++ctx->cur_arg;
                ctx->grouchfsm = -1;
                if (ctx->cur_arg < nr_args) { 
                    set_baud(ctx, &args[ctx->cur_arg]);
                }
            }
        }
    }
    
    if (rv > 0) {
        utils_safe_write(ctx->ttyfd, buf, rv);
    }
    // Anything from the terminal? 
    rv = ctx->bio->read(ctx->bio, buf, 32);
    if (rv > 0) { 
        int i, optr = 0;
        for (i =0 ;i < rv; ++i) { 
            if (ctx->control_mode) { 
                switch (buf[i]) { 
                case 'h': console_help(ctx); break;
                case 's': safe_putty(ctx, "Oh no! Spiders!\n"); break;
                case 'g': groan_with(ctx,0); break;
                case 'x': ret = -1; break;
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
        ctx->bio->write(ctx->bio, buf, optr);
    } else if (rv == 0) { 
        safe_putty(ctx, "! upc2: Input closed.\n");
        ret = -1;
    } else if (rv < 0) { 
        if (!(errno == EINTR || errno = EAGAIN))  {
            safe_putty(cvtx, "! upc2: Failed to read tty - %s [%d] \n", strerror(errno), errno);
            ret = -1;
        }
    }
end:
    return ret;
}

int up_finish_console(up_context_t *ctx) { 
    safe_putty(ctx, "! upc2: Terminating console.\n");
    tcsetattr(ctx->ttyfd, TCSANOW, &ctx->tc);
    fcntl(ctx->ttyfd, F_SETFL, ctx->ttyflags);
    return 0;
}



int up_become_console(up_context_t *ctx, up_load_arg_t *args, int nr_args) { 
    int rv, r2;
    rv = up_start_console(ctx, 0, -1);
    if (rv < 0) { return rv; }
    do { 
        rv = up_operate_console(ctx, args, nr_args);
    } while (rv >= 0);
    r2 = up_finish_console(ctx);
    if (r2 < 0) { return r2; }
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

