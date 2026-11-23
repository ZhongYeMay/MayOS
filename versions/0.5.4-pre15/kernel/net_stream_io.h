#ifndef MAYOS_NET_STREAM_IO_H
#define MAYOS_NET_STREAM_IO_H

/* One nonblocking outbound IPv4 stream. A zero read/write means try again.
 * Receive EOF and failure are reported separately from a temporarily empty buffer. */
enum { MAY_STREAM_IDLE, MAY_STREAM_CONNECTING, MAY_STREAM_READY,
       MAY_STREAM_EOF, MAY_STREAM_CLOSED, MAY_STREAM_FAILED };
int may_stream_open(const unsigned char destination[4], unsigned short port);
int may_stream_status(void);
int may_stream_read(unsigned char *data, unsigned capacity);
int may_stream_write(const unsigned char *data, unsigned length);
void may_stream_close(void);
void may_stream_abort(void);
int may_stream_echo_check(const char *address, const char *port);

#endif
