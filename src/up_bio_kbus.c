/* up_bio_kbus.c */
/* Copyright (c) Kynesim Ltd, 2019 */

/** @file
 *
 * A BIO for Kbus.  This is mostly intended for debugging protocol
 * modules.
 *
 * @author Rhodri James <rhodri@kynesim.co.uk>
 * @date   2019-03-04
 */

#include <stdio.h>
#include <stdlib.h>

#include "kbus/kbus.h"

#include "upc2/up.h"
#include "upc2/up_bio_kbus.h"



static void up_bio_kbus_dispose(up_bio_t *bio)
{
    up_bio_kbus_t *handle = (up_bio_kbus_t *)(bio->handle);

    if (handle->ksock >= 0)
        kbus_ksock_close(handle->ksock);
    if (handle->msg != NULL)
        kbus_msg_delete(&handle->msg);
    free(handle);
    memset(bio, 0, sizeof(up_bio_t)); /* Make life easier for Valgrind */
    free(bio);
}


static int up_bio_kbus_poll_fd(up_bio_t *bio)
{
    up_bio_kbus_t *handle = (up_bio_kbus_t *)(bio->handle);

    return handle->ksock;
}


static int up_bio_kbus_read(up_bio_t *bio, uint8_t *buffer, int nbytes)
{
    up_bio_kbus_t *handle = (up_bio_kbus_t *)(bio->handle);
    int rv;

    if (handle->msg == NULL)
    {
        rv = kbus_ksock_read_next_msg(handle->ksock, &handle->msg);
        if (rv < 0)
            return rv;
        if (handle->msg == NULL)
            return 0; /* Nothing to read */
        handle->data = kbus_msg_data_ptr(handle->msg);
        handle->nbytes = handle->msg->data_len;
    }
    if (handle->nbytes <= nbytes)
    {
        /* This is a single message */
        memcpy(buffer, handle->data, handle->nbytes);
        rv = handle->nbytes;
        kbus_msg_delete(&handle->msg);
        handle->msg = NULL;
        handle->data = NULL;
        handle->nbytes = 0;
        return rv;
    }

    /* Otherwise we have asked for only part of the current message */
    memcpy(buffer, handle->data, nbytes);
    handle->data += nbytes;
    handle->nbytes -= nbytes;
    return nbytes;
}


static int up_bio_kbus_write(up_bio_t *bio, const uint8_t *buffer, int nbytes)
{
    up_bio_kbus_t *handle = (up_bio_kbus_t *)(bio->handle);
    kbus_message_t *msg;
    kbus_msg_id_t id;
    int rv;

    rv = kbus_msg_create(&msg, "$.upc.fromUpc", 13,
                         buffer, nbytes, 0);
    if (rv < 0)
        return rv;
    rv = kbus_ksock_send_msg(handle->ksock, msg, &id);
    if (rv < 0)
        return rv;
    /* Otherwise we sent the *whole* message in one KBus packet */
    return nbytes;
}


static int up_bio_kbus_set_baud(up_bio_t *bio, int baud, int flow_control)
{
    /* We could send a special message with this info, but let's not for now */
    return 0;
}


up_bio_t *up_bio_kbus_create(const char *bus)
{
    up_bio_t *bio = malloc(sizeof(up_bio_t));
    up_bio_kbus_t *handle;
    int rv;

    if (bio == NULL)
        return NULL;
    if ((handle = malloc(sizeof(up_bio_kbus_t))) == NULL)
    {
        free(bio);
        return NULL;
    }

    memset(bio, 0, sizeof(up_bio_t));
    memset(handle, 0, sizeof(up_bio_kbus_t));
    bio->handle = handle;
    bio->dispose = up_bio_kbus_dispose;
    bio->poll_fd = up_bio_kbus_poll_fd;
    bio->read = up_bio_kbus_read;
    bio->write = up_bio_kbus_write;
    bio->safe_write = up_bio_kbus_write;
    bio->set_baud = up_bio_kbus_set_baud;

    handle->ksock = kbus_ksock_open_by_name(bus, O_RDWR);
    if (handle->ksock < 0)
    {
        rv = -(handle->ksock);
        fprintf(stderr, "Unable to open ksocket: %s [%d]\n",
                strerror(rv), rv);
        free(handle);
        free(bio);
        return NULL;
    }

    if ((rv = kbus_ksock_bind(handle->ksock, "$.upc.toUpc", 0)) < 0)
    {
        fprintf(stderr, "Unable to bind to upc messages: %s [%d]\n",
                strerror(-rv), -rv);
        free(handle);
        free(bio);
        return NULL;
    }

    return bio;
}
