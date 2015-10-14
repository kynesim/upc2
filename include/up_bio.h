/* up_bio.h */
/* (C) Kynesim Ltd 2015 */

#ifndef UP_BIO_H_INCLUDED
#define UP_BIO_H_INCLUDED

typedef struct up_bio_struct {
    void *ctx;
    
    /** Retrieve an fd you can poll() on */
    int (*poll_fd)(up_bio_t *bio);

    /** read(), non-blocking */
    int (*read)(up_bio_t *bio, uint8_t *bytes, int nr);

    /** write(), non-blocking */
    int (*write)(up_bio_t *bio, const uint8_t *bytes, int nr);
   
    /** write(), blocking */
    int (*safe_write)(up_bio_t *bio, const uint8_t *bytes, int nr);

    /** set baud rate */
    void (*set_baud)(up_bio_t *bio, int baud);

    /** Dispose of this BIO, closing resources and
     *  releasing the BIO if dynamically allocated
     */
    void (*dispose)(up_bio_t *bio);
} up_bio_t;


#endif

/* End file */
