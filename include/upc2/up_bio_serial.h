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

} up_bio_serial_t;

up_bio_t *up_bio_serial_create(const char *serial_port);

#endif

/* End file */
