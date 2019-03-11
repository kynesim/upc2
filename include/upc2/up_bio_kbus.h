/* up_bio_kbus.h */
/* Copyright (c) Kynesim Ltd, 2019 */

#ifndef UP_BIO_KBUS_H_INCLUDED
#define UP_BIO_KBUS_H_INCLUDED


#include <stdint.h>
#include "upc2/up_bio.h"
#include "kbus/kbus.h"

typedef struct up_bio_kbus_s
{
    kbus_ksock_t ksock;
    kbus_message_t *msg;
    uint8_t *data;
    uint32_t nbytes;
} up_bio_kbus_t;


extern up_bio_t *up_bio_kbus_create(const char *bus);


#endif /* UP_BIO_KBUS_H_INCLUDED */
