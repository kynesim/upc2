/* up.h */
/* (C) Kynesim Ltd 2012-15 */

#ifndef UP_H_INCLUDED
#define UP_H_INCLUDED

/** @file
 *
 *  API for talking to upc/upc2
 *
 * @author Richard Watts <rrw@kynesim.co.uk>
 * @date   2015-10-08
 */

#include <stdint.h>
#include <termios.h>
#include "up_bio.h"

//#define UP_PROTOCOL_GROUCH  (0)
//#define UP_PROTOCOL_XMODEM  (1)

typedef struct up_context_struct {
    /** I/O handle for the interface we are using */
    up_bio_t *bio;

    /** Grouch FSM state; > 0 are real statues, < 0 are used by the
     * protocol machine
     */
    int grouchfsm;

    /** Current upload file being processed */
    int cur_arg;

    /** fd for the tty we use to perform console I/O to/from */
    int ttyfd;

    /** old fd flags for the tty so we can restore them later */
    int ttyflags;

    /** Old terminal properties for console mode so we can
     *  remember to restore them.
     */
    struct termios tc;


    /** Log file descriptor */
    int logfd;

    /** Control mode: 1 => last keypress was ^A */
    int control_mode;
} up_context_t;



typedef struct up_load_arg_struct up_load_arg_t;

typedef struct up_protocol_struct {
    /** Name of the protocol (for command line parsing) */
    const char *name;

    /** Initialise protocol at program start, returns opaque handle */
    void *(*init)(void);

    /** Prepare an individual boot stage */
    int (*prepare)(void *h, up_context_t *ctx, up_load_arg_t *arg);

    /** Transfer function for a boot stage */
    int (*transfer)(void          *h,
                    up_context_t  *ctx,
                    up_load_arg_t *arg,
                    /* Buffer of bytes read in from BIO */
                    const uint8_t *buf,
                    /* Number of bytes in buf */
                    int            buf_bytes);

    /** Complete an individual boot stage */
    int (*complete)(void *h, up_context_t *ctx, up_load_arg_t *arg);

    /** Close down protocol before quitting */
    int (*shutdown)(void *h, up_context_t *ctx);
} up_protocol_t;


typedef struct up_load_arg_struct {
    /** Name to upload */
    const char *file_name;

    /** fd, < 0 for none */
    int fd;

    /** Protocol object for transfer */
    const up_protocol_t *protocol;

    /** Handle for protocol */
    void *protocol_handle;

    /** Baud rate */
    int baud;
} up_load_arg_t;

/** Create a UP context
 */
int up_create(up_context_t **ctxp);

/** Attach a BIO to an UP context; the context claims the BIO and will
 *  call destroy() on it in due course
 */
int up_attach_bio(up_context_t *ctx, up_bio_t *bio);

/** Dispose of an up context and call dispose() on any associated BIO
 */
int up_dispose(up_context_t **ctxp);

/** Start the console
 *
 * @param[in] tty_fd The fd to our controlling tty - either 0 for
 *                     stdin, or a pty you have created.
 */
int up_start_console(up_context_t *ctx, int tty_fd);

/** Operate the console for a bit.
 */
int up_operate_console(up_context_t *ctx, up_load_arg_t *args, int arglen);

/** Finish the conse */
int up_finish_console(up_context_t *ctx);

/** Become a console, wait until someone says it should quit, then
 *  finish it.  Useful for turning your UPC program into a console at
 *  the end
 */
int up_become_console(up_context_t *ctx, up_load_arg_t *args, int arglen);

/** Log all console input to this fd */
int up_set_log_fd(up_context_t *ctx, const int fd);

/** Parse a human readable baud rate into a number */
int up_read_baud(const char *baud);

#endif
