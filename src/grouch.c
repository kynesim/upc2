/* grouch.c */
/* (C) Kynesim Ltd 2015 */

#include "up.h"
#include "utils.h"
#include 

static int grouch(up_context_t *upc, up_load_arg_t *arg);

static int grouch(up_context_t *upc, up_load_arg_t *arg) {
    off_t len;
    uint8_t buf[4096];
    int in_buf = 0;
    int done = 0;

    len = lseek(arg->fd, 0, SEEK_END);
    if (len == (off_t)-1)
    {
        fprintf(stderr, "Cannot lseek() %s - %s [%d] \n", 
                NAME_MAYBE_NULL(arg->file_name),
                strerror(errno), errno);
        return -1;
    }
    lseek(arg->fd, 0, SEEK_SET);
   
    uint32_t sum = 0, wrote_sum = 0;
    // Preload the buffer with the length, BE.
    buf[0] = '*'; // Synchronisation.
    buf[1] = (len >> 24) & 0xff;
    buf[2] = (len >> 16) & 0xff;
    buf[3] = (len >> 8) & 0xff;
    buf[4] = (len & 0xff);
    in_buf = 5;
    
    while (!done || in_buf)
    {
        int rv;
        if (utils_check_critical_control(up) < 0) { return -2; }

        rv = upc->bio->read(up->bio, &buf[in_buf], 4096-in_buf);
        if (rv > 0)
        {
            utils_safe_write(upc->ttyfd, &buf[in_buf], rv);
        }
            
            
        rv = read(arg->fd, &buf[in_buf], 4096-in_buf);
        if (rv < 0) 
        {
            if (!(errno == EINTR || errno == EAGAIN))
            {
                fprintf(stderr, "Error reading grouch file %s - %s [%d] \n", 
                        NAME_MAYBE_NULL(arg->file_name), strerror(errno), errno);
                /** @todo Should stuff the rest of the file and send a deliberately incorrect
                 *   checksum to force restart.
                 */
                return -1;
            }
        }
        else if (!rv)
        {
            if (wrote_sum)
            {
                done = 1;
            }
            else
            {
                if (in_buf < (4096 - 4))
                {
                    buf[in_buf] = (sum >> 24) & 0xff;        
                    buf[in_buf+1] = (sum >> 16) & 0xff;        
                    buf[in_buf+2] = (sum >> 8) & 0xff;        
                    buf[in_buf+3] = (sum >> 0) & 0xff;        
                    in_buf += 4;
                    printf("! grouch complete: host sum = 0x%08x \n", sum);
                    wrote_sum = 1;
                }
            }
        }
        else
        {
            int x;
            // Update sum.
            for (x =0 ; x< rv; ++x)
            {
                sum += buf[in_buf + x];
            }
            in_buf += rv;
        }
        // Now write to the output .. 
        rv = upc->bio->safe_write(up->bio, buf, in_buf);
        // printf("<host> wrote %d / %d\n", rv, in_buf);
        if (rv >= 0)
        {
            //printf("rv = %d, in_buf = %d \n", rv, in_buf);
            memmove(&buf[0], &buf[in_buf], in_buf-rv);
            in_buf -= rv;
        }
    }

    return 0;
}

static int maybe_grouch(up_context_t *ctx, up_load_arg_t *arg, const uint8_t *buf, int rv)
{
    static const char *cue = "*LOAD*";
    static const int cuelen = 6;
    int x;
    for (x =0 ;x < rv; ++x)
    {
        char c = buf[x];
        // Ignore '\0' - dunno why these turn up.
        /** @todo investigate this .. */
        if (!c) continue;
        

        //printf("f = %d c = '%c' (%d)\n", ctx->grouchfsm, c, c);
        if (cue[ctx->grouchfsm] == c)
        {
            ++ctx->grouchfsm;
            if (ctx->grouchfsm == cuelen)
            {
                // Got our cue! Go do it 
                int rv; 
                rv = grouch(ctx, arg);
                // We're either errored or done.
                return (rv < 0 ? -1 : 1);
            }
        }
        else
        {
            ctx->grouchfsm = (cue[0] == c ? 1 : 0);
        }
    }
    // Still hunting.
    return 0; 

}

int up_internal_check_control(up_context_t *ctx)
{
    int rv;
    char bx[2];
    rv = read(ctx->ttyfd, bx, 1);
    if (rv == 1)
    {
        if (!ctx->ctrl && bx[0] == 0x01)
        {
            ctx->ctrl = 1;
            return 0;
        }
        if (ctx->ctrl)
        {
            switch (bx[0])
            {
            case 'x': 
                fprintf(stderr, "Detected C-a C-x ; dying.\n");
                return -2;
            default:
                // No one cares in grouch.
                break;
            }
            ctx->ctrl = 0;
        }
    }
    return 0;
}

/* End File */
