/* up_bio_serial.h */
/* (C) Kynesim Ltd 2015 */

#ifndef UP_BIO_SERIAL_H_INCLUDED
#define UP_BIO_SERIAL_H_INCLUDED

#include "upc2/up_bio.h"

typedef struct up_bio_serial_struct {
    /** For debugging, mostly */
    const char *serial_port;

    int serial_fd;

    struct termios serial_tc;

    int last_flow_control;

} up_bio_serial_t;

/* Allocate and initialise a context structure to access the named
 * serial device (e.g. /dev/ttyUSB0).  The device will be opened and
 * all the function pointers will be filled in.
 */
up_bio_t *up_bio_serial_create(const char *serial_port);

#endif

/* End file */
