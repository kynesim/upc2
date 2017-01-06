/* upc2.c */
/* (C) Kynesim Ltd 2012-15 */

/** @file
 *
 *  The second version of the upc terminal program.
 *
 * @author Richard Watts <rrw@kynesim.co.uk>
 * @author Rhodri James <rhodri@kynesim.co.uk>
 * @date  2015-10-08
 */

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <time.h>
#include <sys/time.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/errno.h>
#include <string.h>

#include "upc2/up.h"
#include "upc2/up_bio_serial.h"
#include "upc2/up_lineend.h"
#include "upc2/grouch.h"
#include "upc2/xmodem.h"
#include "upc2/utils.h"

#define MAX_ARGS 32
#define MAX_SCRIPTS 10
#define DEBUG0 1

/* Structure for the protocol tables */
typedef struct up_parse_protocol_struct
{
    const up_protocol_t *protocol;
    void                *handle;
} up_parse_protocol_t;

up_parse_protocol_t protocols[] = {
    { &grouch_protocol, NULL },
    { &xmodem_protocol, NULL },
    { &xmodem128_protocol, NULL },
    { NULL, NULL }
};

/* Dummy protocol to use for the console arg */
const up_protocol_t dummy_protocol = {
    "console",
    NULL,
    utils_protocol_set_baud,
    NULL,
    NULL,
    NULL
};


/* Structure to allow recursing into scripts */
typedef struct up_parse_stack_struct
{
    char **argv;
    int    argc;
    int    optind;
    char  *buffer;
} up_parse_stack_t;


struct option options[] = {
    { "log",      required_argument, NULL, 'l' },
    { "serial",   required_argument, NULL, 's' },
    { "baud",     required_argument, NULL, 'b' },
    { "fc",       required_argument, NULL, 'f' },
    { "grouch",   required_argument, NULL, 'g' },
    { "protocol", required_argument, NULL, 'p' },
    { "defer",    no_argument,       NULL, 'd' },
    { "script",   required_argument, NULL, 'x' },
    { "lineend",  required_argument, NULL, 'n' },
    { "help",     no_argument,       NULL, 'h' },
    { NULL, 0, NULL, 0 }
};

static void usage(void);
static char *read_script(const char *filename, int *pargn, char ***pargs);
static up_parse_protocol_t *parse_protocol(const char *name);

int main(int argn, char *args[]) {
    up_context_t *upc;
    int rv;
    // The One True Baud Rate as any fule no
    int baud = 115200;
    int log_fd = -1;
    up_load_arg_t up_args[MAX_ARGS + 1];
    up_parse_stack_t parse_stack[MAX_SCRIPTS + 1];
    int cur_arg = -1;
    int cur_script = 0;
    const char *serial_port = "/dev/ttyUSB0";
    int fc = UP_FLOW_CONTROL_RTSCTS;
    int option;
    up_parse_protocol_t *selected_protocol;
    up_translation_table_t *translations = parse_line_end("none");

    memset(&up_args, '\0', sizeof(up_args));
    up_args[0].fd = -1;
    up_args[0].baud = 115200;

    /* We horribly abuse getopt_long() to recurse into script files
     * (which are in fact just command line options in a file).  There
     * is no guarantee that setting optind to anything other than 1
     * works, however it seems to.
     */

    parse_stack[0].argv = args;
    parse_stack[0].argc = argn;
    parse_stack[0].buffer = NULL;

    while (cur_script >= 0)
    {
        while ((option = getopt_long(argn, args,
                                     "l:s:b:g:p:n:hd",
                                     options, NULL)) != -1)
        {
            switch (option)
            {
                case 'l':
                    if (log_fd != -1)
                    {
                        fprintf(stderr, "Log file already open\n");
                        return 3;
                    }
                    log_fd = open(optarg,
                                  O_WRONLY | O_CREAT | O_APPEND,
                                  0x644);
                    if (log_fd < 0)
                    {
                        fprintf(stderr,
                                "Cannot open log file %s: %s [%d]\n",
                                optarg, strerror(errno), errno);
                        return 3;
                    }
                    break;

                case 's':
                    serial_port = optarg;
                    break;

                case 'b':
                    if (cur_arg < 0)
                    {
                        fprintf(stderr, "No boot stage for baud rate %s\n",
                                optarg);
                        return 6;
                    }
                    up_args[cur_arg].baud = up_read_baud(optarg);
                    break;

                case 'g':
                    if (++cur_arg >= MAX_ARGS)
                    {
                        fprintf(stderr, "Only %d upload files allowed\n",
                                MAX_ARGS);
                        return 4;
                    }
                    /* Set a default baud rate */
                    if (cur_arg > 0)
                        up_args[cur_arg].baud = up_args[cur_arg-1].baud;
                    /* Duplicate the file name because we will free
                     * the underlying buffer before it gets used.
                     */
                    up_args[cur_arg].file_name = strdup(optarg);
                    if (up_args[cur_arg].file_name == NULL)
                    {
                        fprintf(stderr, "Out of memory storing filename\n");
                        return 4;
                    }
                    break;

                case 'p':
                    if (cur_arg < 0)
                    {
                        fprintf(stderr, "No boot stage for protocol %s\n",
                                optarg);
                        return 7;
                    }
                    selected_protocol = parse_protocol(optarg);
                    if (selected_protocol == NULL)
                    {
                        fprintf(stderr, "Invalid protocol specified\n");
                        usage();
                        return 5;
                    }
                    up_args[cur_arg].protocol =
                        selected_protocol->protocol;
                    up_args[cur_arg].protocol_handle =
                        selected_protocol->handle;
                    break;

                case 'd':
                    if (cur_arg < 0)
                    {
                        fprintf(stderr, "No boot stage for deferral\n");
                        return 8;
                    }
                    up_args[cur_arg].deferred = 1;
                    break;

                case 'x':
                    if (cur_script >= MAX_SCRIPTS)
                    {
                        fprintf(stderr,
                                "Only %d script recursions allowed\n",
                                MAX_SCRIPTS);
                        return 8;
                    }
                    /* Push our current command line parsing state */
                    parse_stack[cur_script++].optind = optind;
                    /* Read in the file as if it was a command line
                     * and reset the parsing.
                     */
                    parse_stack[cur_script].buffer =
                        read_script(optarg, &argn, &args);
                    parse_stack[cur_script].argc = argn;
                    parse_stack[cur_script].argv = args;
                    optind = 1;
                    break;

                case 'n':
                    /* Line endings */
                    if ((translations = parse_line_end(optarg)) == NULL)
                    {
                        fprintf(stderr, "Unrecognised line-ending '%s'\n",
                                optarg);
                        return 9;
                    }
                    break;
                    

            case 'f':
                if (strstr(optarg, "rts") || strstr(optarg, "cts")) {
                    fc = UP_FLOW_CONTROL_RTSCTS;
                } else if (!strcmp(optarg, "none")) {
                    fc = UP_FLOW_CONTROL_NONE;
                } else {
                    fprintf(stderr, "Unrecognised flow control spec '%s'\n", 
                            optarg);
                    return 3;
                }
                break;
                    

                default:
                    /* Includes "--help" */
                    usage();
                    return 9;
            }
        }

        if (optind != argn && optind != argn-1)
        {
            fprintf(stderr, "Extra arguments on command line\n");
            usage();
            exit(1);
        }
        if (optind == argn - 1)
        {
            baud = up_read_baud(args[optind]);
        }

        /* Pop the parse stack, freeing any file buffers */
        free(parse_stack[cur_script].buffer);
        if (--cur_script >= 0)
        {
            /* Readers of a nervouse disposition may wish to look away */
            args = parse_stack[cur_script].argv;
            argn = parse_stack[cur_script].argc;
            optind = parse_stack[cur_script].optind;
        }
    }

    /* Safe because up_args contains MAX_ARGS + 1 elements */
    ++cur_arg;
    up_args[cur_arg].fd = -1;
    up_args[cur_arg].baud = baud;
    up_args[cur_arg].protocol = &dummy_protocol;
    up_args[cur_arg].fc = fc;


    /* Now open all the files and do the protocol preparation */
    {
        int i;

        for (i = 0; i < cur_arg; ++i)
        {
            if (up_args[i].protocol == NULL)
            {
                up_args[i].protocol = protocols[0].protocol;
                if (protocols[0].handle == NULL &&
                    protocols[0].protocol->init != NULL)
                {
                    protocols[0].handle = protocols[0].protocol->init();
                    if (protocols[0].handle == NULL)
                    {
                        fprintf(stderr, "Error initialising %s\n",
                                protocols[0].protocol->name);
                        return 1;
                    }
                }
                up_args[i].protocol_handle = protocols[0].handle;
            }

            if (up_args[i].file_name)
            {
                up_args[i].fd = open(up_args[i].file_name, O_RDONLY);
                if (up_args[i].fd < 0)
                {
                    fprintf(stderr, "Cannot open %s: %s [%d]\n",
                            up_args[i].file_name, strerror(errno), errno);
                    return 1;
                }
            }
            else
            {
                up_args[cur_arg].fd = -1;
            }

#if DEBUG0
            printf("arg[%d] = { file_name = %s, fd = %d, protocol = %s,"
                   " baud = %d }\n",
                   i, up_args[i].file_name, up_args[i].fd,
                   up_args[i].protocol->name, up_args[i].baud);
#endif

        }
    }

    rv = up_create(&upc, translations);
    if (rv < 0)
    {
        fprintf(stderr, "Cannot create upc context\n");
        goto end;
    }

    /* Open a serial port */
    up_bio_t *bio = up_bio_serial_create(serial_port);
    if (!bio) {
        fprintf(stderr, "Cannot create serial BIO for %s.\n", serial_port);
        goto end;
    }

    rv = up_attach_bio(upc, bio);
    if (rv < 0) {
        fprintf(stderr, "Cannot attach serial BIO for %s \n", serial_port);
        goto end;
    }

    printf("Starting console at %d baud, flow control %s with %d arguments...\n",
           baud, utils_decode_flow_control( fc ) , cur_arg);
    fflush(stdout);

    up_set_log_fd(upc, log_fd);

    /* Mine stdin is made the fool o'the FTDI .. */
    up_become_console(upc, up_args, cur_arg+1);

    {
        int i;

        for (i = 0; i < cur_arg; ++i)
        {
            if (up_args[i].fd > -1)
            {
                close(up_args[i].fd);
                free((void *)up_args[i].file_name);
            }
            if (up_args[i].protocol->shutdown != NULL)
                up_args[i].protocol->shutdown(up_args[i].protocol_handle,
                                              upc);
        }
    }


end:
    up_dispose(&upc);
    return rv;
}


static void usage(void)
{
    printf("Syntax: upc2 [--serial /dev/ttyUSBX] [--log file]\n"
           "\t\t[--lineend line-ending]\n"
           "\t\t[--grouch filename [--protocol proto] [--baud baud]]*\n"
           "\t\t[--script filename]*\n"
           "\t\t[<baud>]\n"
           "\n"
           "\t--serial <device> \tUse the given serial device.\n"
           "\t--log <file> \t\tAppend all console input to this file.\n"
           "\t--lineend <line-end> \tTranslate line endings in console.\n"
           "\t\t<line-end> can be 'none' or a string made up of the line\n"
           "\t\tend sequence expected on the host, the character '2', and\n"
           "\t\tline end sequence expected on the target.  For example,\n"
           "\t\t'crlf2lf' translates the host '\\r\\n' sequence to '\\n'\n"
           "\t\ton the target, and vice versa.\n"
           "\t--grouch <filename> \tUpload the given file.\n"
           "\t--baud <rate> \t\tChange baud rate.\n"
           "\t--fc   <none|rtscts>\tSet flow control.\n"
           "\t--defer \t\t Defer this boot stage until invoked by eg. C-a n \n"
           "\t--protocol <proto> \tChange protocol for upload.  \n"
           "Valid protocols:\n"
           "\t\tgrouch \t(default)\n"
           "\t\txmodem\n"
           "\t\txmodem128 - this is like xmodem but forces the block size \n"
           "\t\t\tto 128, as required by Telegesis Zigbee modules\n"
           "\n"
           "You may specify '1m' as your baud rate for 1Mbaud.\n"
           "The final <baud>, if specified, is the baud rate to switch"
           " to for the serial\nconsole once all uploads are finished.\n"
           "\n"
           "You may specify multiple --grouch arguments; each introduces a"
           " new file to\nupload. So to upload xmodem @ 9600 followed by"
           " grouch @ 1m, then switch to\n115200: \n"
           "\n"
           " upc2 --grouch myfile.xmodem --protocol xmodem --baud 9600"
           " --grouch myfile.grouch --protocol grouch --baud 1m 115200\n"
           "\n"
           "Script files are simply collections of command line arguments"
           " exactly as they\nwould appear on the command line, except"
           " that any whitespace (including\nnewlines) may separate tokens"
           " and quotes do not work to 'escape' whitespace.\n"
        );
}


/* Read in a text file and parse it as if it was command-line arguments */
static char *read_script(const char *filename, int *pargn, char ***pargs)
{
    int argc = 1;
    char **argv = malloc(sizeof(char *));
    int fd;
    char *buffer;
    off_t bytes_to_read;
    int bytes_read;
    char *p;

    /* We need an initial argv[0] for getopt to ignore */
    if (argv == NULL)
    {
        fprintf(stderr, "Out of memory parsing script %s\n", filename);
        return NULL;
    }
    argv[0] = NULL;

    fd = open(filename, O_RDONLY);
    if (fd < 0)
    {
        fprintf(stderr, "Cannot open script file %s: %s [%d]\n",
                filename, strerror(errno), errno);
        return NULL;
    }

    /* Determine how big the file is */
    bytes_to_read = lseek(fd, 0, SEEK_END);
    if (bytes_to_read == (off_t)-1)
    {
        fprintf(stderr, "Cannot lseek() %s: %s [%d]\n",
                filename, strerror(errno), errno);
        close(fd);
        return NULL;
    }
    lseek(fd, 0, SEEK_SET);

    /* Read in the whole script file in one go */
    buffer = malloc(bytes_to_read);
    if (buffer == NULL)
    {
        fprintf(stderr, "Out of memory reading script file %s\n",
                filename);
        close(fd);
        return NULL;
    }

    p = buffer;
    while (bytes_to_read > 0)
    {
        bytes_read = utils_safe_read(fd, (uint8_t *)p, bytes_to_read);
        if (bytes_read < 0)
        {
            fprintf(stderr, "Error reading %s: %s [%d]\n",
                    filename, strerror(errno), errno);
            free(buffer);
            close(fd);
            return NULL;
        }
        else if (bytes_read == 0)
        {
            fprintf(stderr, "Unexpected EOF reading %s\n", filename);
            free(buffer);
            close(fd);
            return NULL;
        }
        p += bytes_read;
        bytes_to_read -= bytes_read;
    }

    close(fd);

    /* Now start tokenising */
    /* We use strtok() here, which isn't quite right.  If we were
     * strictly imitating the command line we should pay attention to
     * quotes (and possibly brackets and other shell-like things).
     * However there's no real need to add such complexity, so we keep
     * it simple.
     */
    p = strtok(buffer, " \t\r\n");
    while (p != NULL)
    {
        char **q;

        if ((q = realloc(argv, ++argc * sizeof(char *))) == NULL)
        {
            fprintf(stderr, "Out of memory parsing %s\n", filename);
            free(argv);
            free(buffer);
            return NULL;
        }
        argv = q;
        argv[argc-1] = p;
        p = strtok(NULL, " \t\r\n");
    }

    *pargs = argv;
    *pargn = argc;
    return buffer;
}


static up_parse_protocol_t *parse_protocol(const char *name)
{
    up_parse_protocol_t *p;

    for (p = &protocols[0]; p->protocol != NULL; p++)
    {
        if (!strcmp(name, p->protocol->name))
        {
            /* This is the protocol we want */
            if (p->handle == NULL && p->protocol->init != NULL)
            {
                p->handle = p->protocol->init();
                if (p->handle == NULL)
                {
                    fprintf(stderr, "Error initialising %s\n",
                            p->protocol->name);
                    return NULL;
                }
            }
            return p;
        }
    }

    return NULL;
}
