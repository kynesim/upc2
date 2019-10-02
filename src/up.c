/* up.c */
/* (C) Kynesim Ltd 2012-16 */

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
#include "upc2/up_lineend.h"

#define NAME_MAYBE_NULL(n) (((n) == NULL) ? "(no file name)" : (n))

static void groan_with(up_context_t *ctx, int which);
static void console_help(up_context_t *upc);
static void list_boot_stages(up_context_t  *ctx,
                             up_load_arg_t *args,
                             int            nr_args);
static void continue_boot(up_context_t  *upc,
                          up_load_arg_t *args,
                          int            nr_args);
static void select_boot(up_context_t  *upc,
                        up_load_arg_t *args,
                        int            nr_args,
                        int            selection);

static int hex_of(uint8_t *out_buf, const uint8_t *in_buf,
                  int nr_translate);

static void console_help(up_context_t *upc) {
    utils_safe_printf(upc,
                      "\n"
                      "upc2 0.2 (C) Kynesim Ltd 2012-5\n"
                      "\n"
                      "Console help\n"
                      "\n"
                      "C-a h                This help message\n"
                      "C-a l                List the boot stages\n"
                      "C-a c                Continue paused boot\n"
                      "C-a <digit>          Select boot stage <digit>\n"
                      "C-a n                Select next boot stage\n"
                      "C-a p                Select previous boot stage\n"
                      "C-a e <c1> <c2>      Change line endings\n"
                      "C-a x                Quit.\n"
                      "C-a C-a              Literal C-a \n"
                      "C-a <anything else>  Spiders?\n"
                      "\n"
                      "The C-a e sequence changes the line end encoding"
                      " in use.  The two following\ncharacters must be"
                      " 'l' for LF, 'c' for CR, or 'n' for a CRLF"
                      " sequence.\n<c1> represents the encoding at the"
                      " host end, and <c2> represents the encoding\nat"
                      " the remote end.  For no encoding, use 'C-a e n n'."
                      "\n\n");
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

static void list_boot_stages(up_context_t  *upc,
                             up_load_arg_t *args,
                             int            nr_args)
{
    int i;

    utils_safe_printf(upc, "\n");
    for (i = 0; i < nr_args; i++)
    {
        utils_safe_printf(upc, "[[ %c Boot stage %d: %s %s @ %d fc %s off %#x ]]\n",
                          (i == upc->cur_arg) ? '*' : ' ',
                          i, args[i].protocol->name,
                          NAME_MAYBE_NULL(args[i].file_name),
                          args[i].baud,
                          utils_decode_flow_control( args[i].fc ),
                          args[i].offset);
    }
}

static void continue_boot(up_context_t  *upc,
                          up_load_arg_t *args,
                          int            nr_args)
{
    if (!upc->console_mode)
        return;

    upc->console_mode = (upc->cur_arg >= nr_args ||
                         args[upc->cur_arg].fd < 0);
    if (upc->console_mode)
        utils_safe_printf(upc, "[[ No upload to continue ]]\n");
    else
        utils_safe_printf(upc, "[[ Continuing ]]\n");
}

static void select_boot(up_context_t  *upc,
                        up_load_arg_t *args,
                        int            nr_args,
                        int            selection)
{
    if (!upc->console_mode)
        return;
    if (upc->cur_arg == selection)
    {
        utils_safe_printf(upc, "[[ Boot stage %d: %s %s @ %d fc %s off %#x ]]\n",
                          selection,
                          args[selection].protocol->name,
                          NAME_MAYBE_NULL(args[selection].file_name),
                          args[selection].baud,
                          utils_decode_flow_control(args[selection].fc),
                          args[selection].offset);
        return;
    }

    /* Complete stage upc->cur_arg */
    if (args[upc->cur_arg].protocol->complete != NULL)
        args[upc->cur_arg].protocol->complete(
            args[upc->cur_arg].protocol_handle,
            upc, &args[upc->cur_arg]);

    upc->cur_arg = selection;
    utils_safe_printf(upc, "[[ Boot stage %d: %s %s @ %d / %s to %#x ]]\n",
                      selection,
                      args[selection].protocol->name,
                      NAME_MAYBE_NULL(args[selection].file_name),
                      args[selection].baud,
                      utils_decode_flow_control(args[selection].fc),
                      args[selection].offset);

    /* Prepare stage selection */
    if (args[selection].protocol->prepare != NULL)
        args[selection].protocol->prepare(args[selection].protocol_handle,
                                          upc, &args[selection]);
}

static void next_boot(up_context_t  *upc,
                      up_load_arg_t *args,
                      int            nr_args)
{
    if (upc->cur_arg + 1 >= nr_args)
    {
        utils_safe_printf(upc, "[[ No next boot stage ]]\n");
        return;
    }
    select_boot(upc, args, nr_args, upc->cur_arg + 1);
}

static void previous_boot(up_context_t  *upc,
                          up_load_arg_t *args,
                          int            nr_args)
{
    if (upc->cur_arg - 1 < 0)
    {
        utils_safe_printf(upc, "[[ No previous boot stage ]]\n");
        return;
    }
    select_boot(upc, args, nr_args, upc->cur_arg - 1);
}


int up_create(up_context_t **ctxp, up_translation_table_t * translation) {
    up_context_t *ctx = NULL;
    int rv = 0;

    ctx = (up_context_t *)malloc(sizeof(up_context_t));
    memset(ctx, '\0', sizeof(up_context_t));
    ctx->logfd = ctx->ttyfd = -1;
    ctx->trn = translation;
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
    uint8_t buf[512];
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
        uint8_t *out_buf = buf;
        uint8_t *x_buf = buf;

        if (ctx->console_mode) {
            if (ctx->hex_mode) {
                /* We only ever read up to 32 bytes at a time,
                 * so reuse some of our nice big buffer.
                 */
                x_buf = out_buf;
                out_buf = buf+64;
                rv = hex_of( out_buf, x_buf, rv );
            }
            if (ctx->trn != NULL) {
                /* We only ever read up to 32 bytes at a time,
                 * so reuse some of our nice big buffer.
                 */
                x_buf = out_buf;
                out_buf = buf+256;
                rv = translate_buffer(out_buf, x_buf,
                                      rv, &ctx->trn->from_serial);
            } 
        }
        if (rv) {
            if (cur_arg->echo)
                utils_safe_write(ctx->ttyfd, out_buf, rv);
            if (ctx->logfd >= 0) {
                utils_safe_write(ctx->logfd, out_buf, rv);
            }
        }
    }

    /* Run protocol state machines */
    if (!ctx->console_mode) {
        // Run the state machine.
        ret = cur_arg->protocol->transfer(cur_arg->protocol_handle,
                                          ctx, cur_arg,
                                          buf, rv);

        // If ret < 0, something went wrong
        if (ret < 0)
            goto end;
        // If ret > 0, we've terminated. Move to the next argument and
        // prep the protocol.
        if (ret > 0) {
            if (cur_arg->protocol->complete != NULL)
                cur_arg->protocol->complete(cur_arg->protocol_handle,
                                            ctx, cur_arg);
            cur_arg = &args[++ctx->cur_arg];
            utils_safe_printf(ctx, "[[ Boot stage %d: %s %s @ %d fc %s ]]\n",
                              ctx->cur_arg,
                              cur_arg->protocol->name,
                              NAME_MAYBE_NULL(cur_arg->file_name),
                              cur_arg->baud,
                              utils_decode_flow_control(cur_arg->fc));
            ctx->console_mode = (ctx->cur_arg >= nr_args ||
                                 cur_arg->fd < 0 ||
                                 cur_arg->deferred);
            if (ctx->console_mode)
                utils_safe_printf(ctx, "[[ Entering Console Mode ]]\n");
            if (ctx->cur_arg < nr_args) {
                if (cur_arg->protocol->prepare != NULL)
                    cur_arg->protocol->prepare(cur_arg->protocol_handle,
                                               ctx, cur_arg);
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
        uint8_t *out_buf = buf + 128; /* Again, use upper buffer space */

        for (i = 0; i < rv; ++i) {
            if (ctx->control_mode == 3) {
                /* Check if there is a translation entry with this code */
                up_translation_table_t *trn =
                    parse_escape_line_end(ctx->trn_tag, buf[i]);
                if (trn != NULL) {
                    utils_safe_printf(
                        ctx,
                        "! upc2: Line end sequence changed.\n");
                    ctx->trn = trn;
                } else {
                    utils_safe_printf(ctx,
                                      "! upc2: Unknown line end sequence"
                                      " %c%c\n",
                                      ctx->trn_tag, buf[i]);
                }
                ctx->control_mode = 0;
            } else if (ctx->control_mode == 2) {
                /* First of two characters defining line end */
                ctx->trn_tag = buf[i];
                ctx->control_mode = 3;
            } else if (ctx->control_mode == 1) {
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
                    case 'l':
                        list_boot_stages(ctx, args, nr_args);
                        break;
                    case 'c':
                        continue_boot(ctx, args, nr_args);
                        break;
                    case 'x':
                        ret = -1;
                        break;
                    case 'e':
                        ctx->control_mode = 2;
                        break;
                    case '0':
                    case '1':
                    case '2':
                    case '3':
                    case '4':
                    case '5':
                    case '6':
                    case '7':
                    case '8':
                    case '9':
                        select_boot(ctx, args, nr_args, buf[i] - '0');
                        break;
                    case 'n':
                        next_boot(ctx, args, nr_args);
                        break;
                    case 'p':
                        previous_boot(ctx, args, nr_args);
                        break;
                    default:
                        // Literal whatever-it-is.
                        /* Do put it through the translator */
                        if (ctx->trn != NULL)
                        {
                            uint32_t result =
                                ctx->trn->to_serial.translate(
                                    buf[i], &ctx->trn->to_serial);

                            while (result & TN_CALL_AGAIN)
                            {
                                if (!(result & TN_SUPPRESS))
                                    out_buf[optr++] = result & 0xff;
                                result = ctx->trn->to_serial.translate(
                                    '\0', &ctx->trn->to_serial);
                            }
                            if (!(result & TN_SUPPRESS))
                                out_buf[optr++] = result & 0xff;
                        }
                        else
                        {
                            /* No translator for some reason */
                            out_buf[optr++] = buf[i];
                        }
                }
                if (ctx->control_mode == 1)
                    ctx->control_mode = 0;
            } else if (buf[i] == 0x01) { // C-a
                ctx->control_mode = 1;
            } else if (ctx->trn != NULL) { // Allow CRLF translation
                uint32_t result =
                    ctx->trn->to_serial.translate(
                        buf[i], &ctx->trn->to_serial);

                while (result & TN_CALL_AGAIN)
                {
                    if (!(result & TN_SUPPRESS))
                        out_buf[optr++] = result & 0xff;
                    result = ctx->trn->to_serial.translate(
                        buf[i], &ctx->trn->to_serial);
                }
                if (!(result & TN_SUPPRESS))
                    out_buf[optr++] = result & 0xff;
            } else {
                out_buf[optr++] = buf[i];
            }
        }
        /** @todo Don't echo while downloads are ongoing? */
        if (optr > 0)
            ctx->bio->write(ctx->bio, out_buf, optr);
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
    utils_safe_printf(ctx, "[[ Boot stage 0: %s %s @ %d fc %s ]]\n",
                      args[0].protocol->name,
                      NAME_MAYBE_NULL(args[0].file_name),
                      args[0].baud,
                      utils_decode_flow_control(args[0].fc));
    ctx->console_mode = (args[0].fd < 0 || args[0].deferred);
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

static int hex_of(uint8_t *out_buf, const uint8_t *in_buf,
                  int nr_translate) {
    char *outp = (char *)out_buf;
    int nr;
    
    for (nr = 0; nr < nr_translate; ++nr) {
        if ( in_buf[nr] == '\n' ||
             in_buf[nr] == '\r' ||
             ( in_buf[nr] >= 0x20 && 
               in_buf[nr] < 0x7e ) ) {
            *outp++ = in_buf[nr];
        } else {
            outp += sprintf(outp, "[%02x]", in_buf[nr]);
        }
    }
    
    return outp-((char *)out_buf);
}
/* End file */

