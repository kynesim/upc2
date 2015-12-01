/* up_bio.h */
/* (C) Kynesim Ltd 2015 */

#ifndef UP_BIO_H_INCLUDED
#define UP_BIO_H_INCLUDED

#include <stdint.h>

typedef struct up_bio_struct {
    void *ctx;

    /** Retrieve an fd you can poll() on */
    int (*poll_fd)(struct up_bio_struct *bio);

    /** read(), non-blocking */
    int (*read)(struct up_bio_struct *bio, uint8_t *bytes, int nr);

    /** write(), non-blocking */
    int (*write)(struct up_bio_struct *bio, const uint8_t *bytes, int nr);

    /** write(), blocking */
    int (*safe_write)(struct up_bio_struct *bio,
                      const uint8_t        *bytes,
                      int                   nr);

    /** set baud rate */
    int (*set_baud)(struct up_bio_struct *bio, int baud);

    /** Dispose of this BIO, closing resources and releasing the BIO
     *  if dynamically allocated
     */
    void (*dispose)(struct up_bio_struct *bio);
} up_bio_t;


#endif

/* End file */
