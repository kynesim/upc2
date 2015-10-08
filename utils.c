
int up_safe_write(int fd, const uint8_t *data, int len) { 
    int done = 0, rv;
    while (done < len) { 
        rv = write(fd, &data[done], len-done);
        if (rv < 0) { 
            if (errno == EINTR || errno == EAGAIN) continue;
            fprintf(stderr, "! Cannot write(fd=%d) - %s [%d]\n", strerror(errno), errno);
            return -1;
        }
        done += rv;
    }
    return done;
}
