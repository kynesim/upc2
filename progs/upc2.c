/* upc2.c */
/* (C) Kynesim Ltd 2012-15 */

/** @file
 *
 *  The second version of the upc terminal program.
 *
 * @author Richard Watts <rrw@kynesim.co.uk>
 * @date  2015-10-08
 */

#include <stdio.h>
#include <stdlib.h>
#include <upc2/up.h>
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
    
static void usage(void);

int main(int argn, char *args[]) {
    up_context_t *upc;
    int rv;
    // The One True Baud Rate as any fule no
    int baud = 115200; 
    int log_fd = -1;
    up_load_arg_t up_args[MAX_ARGS + 1];
    int cur_arg = 0;
    const char *serial_port = "/dev/ttyUSB0";
    
    memset(&up_args, '\0', sizeof(up_args));
    up_args[0].fd = -1;

    while (argn > 1 && *(args[1]) == '-')
    {
        const char *opt = args[1];

        if (!strcmp(opt, "--log"))
        {
            if (argn > 2)
            {
                log_fd = open(args[2], O_WRONLY | O_CREAT | O_APPEND, 0644);
                if (log_fd < 0)
                {
                    fprintf(stderr, "Cannot open log file %s - %s [%d] \n", 
                            args[2], strerror(errno), errno);
                    return 3;
                }
                --argn; ++args;
            }
            else
            {
                fprintf(stderr, "--log requires an argument.\n");
                usage();
                return 2;
            }
        }
        else if (!strcmp(opt, "--serial"))
        {
            if (argn > 2)
            {
                serial_port  = args[2];
                --argn; ++args;
            }
            else
            {
                fprintf(stderr, "--serial requires an argument.\n");
                usage();
                return 2;
            }
        }
        else if (!strcmp(opt, "--baud"))
        {
            if (argn > 2)
            {
                upargs[cur_arg].baud = up_read_baud(args[2]);
                --argn; ++args;
            }
            else
            {
                fprintf(stderr, "--baud requires an argument.\n");
                usage();
                return 4;
            }
        }
        else if (!strcmp(opt, "--grouch"))
        {
            if (argn > 2)
            {
                if (upargs[cur_arg].file_name)
                {
                    ++cur_arg;
                    if (cur_arg >= MAX_ARGS)
                    {
                        fprintf(stderr, "Only %d upload files allowed - sorry. \n", 
                                MAX_ARGS);
                        return 4;
                    }
                }
                upargs[cur_arg].file_name = args[2];
                --argn; ++args;
            }
            else
            {
                fprintf(stderr, "--grouch requires an argument.\n");
                usage();
                return 3;
            }
        }
        else if (!strcmp(opt, "--protocol"))
        {
            if (argn > 2)
            {
                if (!strcmp(args[2], "grouch"))
                {
                    upargs[cur_arg].protocol = UP_PROTOCOL_GROUCH;
                }
                else if (!strcmp(args[2], "xmodem"))
                {
                    upargs[cur_arg].protocol = UP_PROTOCOL_XMODEM;
                }
                else
                {
                    fprintf(stderr, "Invalid protocol specified: need grouch or xmodem.\n");
                    usage();
                    return 5;
                }
                --argn; ++args;
            }
            else
            {
                fprintf(stderr, "--protocol requires an argument.\n");
                usage();
                return 4;
            }
        }
        else if (!strcmp(opt, "--help") || !strcmp(opt, "-h") ||
                 !strcmp(opt, "-?"))
        {
            usage();
            return 9;
        }
        else
        {
            fprintf(stderr, "Syntax error: Invalid option '%s'\n", opt);
            usage();
            return 10;
        }
            

        --argn; ++args;
    }

    if (argn != 1 && argn != 2)
    {
        fprintf(stderr, "Extra arguments on command line.\n");
        usage();
        exit(1);
    }
    
    if (argn == 2) 
    {
        baud = up_read_baud(args[1]);
    }

    if (upargs[cur_arg].file_name) 
    {
        ++cur_arg;
    }
    upargs[cur_arg].fd = -1;
    upargs[cur_arg].baud = baud;


    /* Now open all the files .. */
    {
        int i;
        for (i =0 ;i < cur_arg; ++i)
        {
            if (upargs[i].file_name)
            {
                upargs[i].fd = open(upargs[i].file_name, O_RDONLY);
                if (upargs[i].fd < 0)
                {
                    fprintf(stderr, "Cannot open %s - %s [%d] \n",
                            upargs[i].file_name, strerror(errno), errno);
                    return 1;
                }
            }
            else
            {
                upargs[cur_arg].fd = -1;
            }

#if DEBUG0
            printf("arg[%d] = { file_name = %s, fd = %d, protocol = %d, baud = %d } \n",
                   i, upargs[i].file_name, upargs[i].fd, upargs[i].protocol,
                   upargs[i].baud);
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

    printf("Starting console at %d baud with %d arguments .. \n", baud, cur_arg); fflush(stdout);
    
    up_set_log_fd(upc, log_fd);

    /* Mine stdin is made the fool o'the FTDI .. */
    up_become_console(upc, upargs, cur_arg+1);

    {
        int i;
        for (i =0 ;i < cur_arg; ++i)
        {
            if (upargs[cur_arg].fd >  -1) 
            {
                close(upargs[cur_arg].fd); 
            }
        }
    }


end:        
    up_dispose(&upc);
    return rv;
}

static void usage(void)
{
    printf("Syntax: upc [--serial /dev/ttyUSBX] [--log file]  [--grouch filename] [--aftergrouch <baud>] <baud> \n"
           "\n"
           "--serial <device>     Use the given serial device instead of a universal programmer.\n"
           "--grouch <filename>   If you see the target issue *LOAD*, grouchload the given file.\n"
           "--aftergrouch <baud>  Switch baud rate to this after grouchload.\n"
           "--protocol <xmodem|grouch> Change protocol for upload.\n"
           "--log <file>          Append all console input to this file. \n"
           "\n"
           " You may specify '1m' as your baud rate for 1Mbaud.\n"
           " Start the universal programmer console at the given baud rate.\n"
           "\n"
           "You may specify multiple --grouch arguments; each introduces a new file to upload. So to upload\n"
           " xmodem  @ 9600 followed by grouch @ 1m, then switch to 115200: \n"
           "\n"
           " upc --grouch myfile.xmodem --protocol xmodem --baud 9600 --grouch myfile.grouch --protocol grouch\n"
           "  --baud 1m 115200\n"
        );
}

/* End file */

