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
#include <upc2/up.h>
#include <upc2/up_bio_serial.h>
#include <time.h>
#include <sys/time.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/errno.h>
#include <string.h>

#define MAX_ARGS 32
#define DEBUG0 1


struct option options[] = {
    { "log",      required_argument, NULL, 'l' },
    { "serial",   required_argument, NULL, 's' },
    { "baud",     required_argument, NULL, 'b' },
    { "grouch",   required_argument, NULL, 'g' },
    { "protocol", required_argument, NULL, 'p' },
    { "help",     no_argument,       NULL, 'h' },
    { NULL, 0, NULL, 0 }
};

static void usage(void);

int main(int argn, char *args[]) {
    up_context_t *upc;
    int rv;
    // The One True Baud Rate as any fule no
    int baud = 115200;
    int log_fd = -1;
    up_load_arg_t up_args[MAX_ARGS + 1];
    int cur_arg = -1;
    const char *serial_port = "/dev/ttyUSB0";
    int option;

    memset(&up_args, '\0', sizeof(up_args));
    up_args[0].fd = -1;
    up_args[0].baud = 115200;

    while ((option = getopt_long(argn, args,
                                 "l:s:b:g:p:h",
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
                    fprintf(stderr, "Cannot open log file %s: %s [%d]\n",
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
                up_args[cur_arg].file_name = optarg;
                break;

            case 'p':
                if (cur_arg < 0)
                {
                    fprintf(stderr, "No boot stage for protocol %s\n",
                            optarg);
                    return 7;
                }
                if (!strcmp(optarg, "grouch"))
                    up_args[cur_arg].protocol = UP_PROTOCOL_GROUCH;
                else if (!strcmp(optarg, "xmodem"))
                    up_args[cur_arg].protocol = UP_PROTOCOL_XMODEM;
                else
                {
                    fprintf(stderr, "Invalid protocol specified:"
                            " must be 'grouch' or 'xmodem'\n");
                    usage();
                    return 5;
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
        baud = up_read_baud(args[optind]);

    /* Safe because up_args contains MAX_ARGS + 1 elements */
    ++cur_arg;
    up_args[cur_arg].fd = -1;
    up_args[cur_arg].baud = baud;


    /* Now open all the files .. */
    {
        int i;
        for (i = 0; i < cur_arg; ++i)
        {
            if (up_args[i].file_name)
            {
                up_args[i].fd = open(up_args[i].file_name, O_RDONLY);
                if (up_args[i].fd < 0)
                {
                    fprintf(stderr, "Cannot open %s: %s [%d] \n",
                            up_args[i].file_name, strerror(errno), errno);
                    return 1;
                }
            }
            else
            {
                up_args[cur_arg].fd = -1;
            }

#if DEBUG0
            printf("arg[%d] = { file_name = %s, fd = %d, protocol = %d,"
                   " baud = %d }\n",
                   i, up_args[i].file_name, up_args[i].fd,
                   up_args[i].protocol, up_args[i].baud);
#endif

        }
    }

    rv = up_create(&upc);
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

    printf("Starting console at %d baud with %d arguments...\n",
           baud, cur_arg);
    fflush(stdout);

    up_set_log_fd(upc, log_fd);

    /* Mine stdin is made the fool o'the FTDI .. */
    up_become_console(upc, up_args, cur_arg+1);

    {
        int i;
        for (i = 0; i < cur_arg; ++i)
        {
            if (up_args[cur_arg].fd > -1)
            {
                close(up_args[cur_arg].fd);
            }
        }
    }


end:
    up_dispose(&upc);
    return rv;
}


static void usage(void)
{
    printf("Syntax: upc2 [--serial /dev/ttyUSBX] [--log file]\n"
           "\t\t[--grouch filename [--protocol proto] [--baud baud]]*\n"
           "\t\t[<baud>]\n"
           "\n"
           "\t--serial <device> \tUse the given serial device.\n"
           "\t--log <file> \t\tAppend all console input to this file.\n"
           "\t--grouch <filename> \tUpload the given file.\n"
           "\t--baud <rate> \t\tChange baud rate.\n"
           "\t--protocol <proto> \tChange protocol for upload.  "
           "Valid protocols:\n"
           "\t\tgrouch \t(default)\n"
           "\t\txmodem\n"
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
        );
}

/* End file */

