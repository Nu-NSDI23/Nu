/*
 * tcp.h - TCP sockets
 */

#pragma once

#include <base/compiler.h>
#include <errno.h>
#include <net/ip.h>
#include <runtime/net.h>
#include <sys/socket.h>
#include <sys/uio.h>

struct tcpqueue;
typedef struct tcpqueue tcpqueue_t;
struct tcpconn;
typedef struct tcpconn tcpconn_t;

extern int tcp_accept(tcpqueue_t *q, tcpconn_t **c_out);
extern void tcp_qshutdown(tcpqueue_t *q);
extern void tcp_qclose(tcpqueue_t *q);
extern struct netaddr tcp_local_addr(tcpconn_t *c);
extern struct netaddr tcp_remote_addr(tcpconn_t *c);
extern int tcp_shutdown(tcpconn_t *c, int how);
extern void tcp_abort(tcpconn_t *c);
extern void tcp_close(tcpconn_t *c);
extern bool tcp_wait_for_read(tcpconn_t *c);
extern bool tcp_has_pending_data_to_read(tcpconn_t *c);

extern ssize_t __tcp_write(tcpconn_t *c, const void *buf, size_t len, bool nt,
                           bool poll);
extern ssize_t __tcp_writev(tcpconn_t *c, const struct iovec *iov, int iovcnt,
                            bool nt, bool poll);
extern ssize_t __tcp_read(tcpconn_t *c, void *buf, size_t len, bool nt,
			  bool poll);
extern ssize_t __tcp_readv(tcpconn_t *c, const struct iovec *iov, int iovcnt,
                           bool nt, bool poll);

extern int __tcp_dial(struct netaddr laddr, struct netaddr raddr,
		      tcpconn_t **c_out, uint8_t dscp, bool poll);
extern int __tcp_dial_affinity(uint32_t affinity, struct netaddr raddr,
                               tcpconn_t **c_out, uint8_t dscp);
extern int __tcp_dial_conn_affinity(tcpconn_t *in, struct netaddr raddr,
                                    tcpconn_t **c_out, uint8_t dscp);

extern int __tcp_listen(struct netaddr laddr, int backlog, tcpqueue_t **q_out,
                        uint8_t dscp);

/**
 * tcp_write - writes data to a TCP connection
 * @c: the TCP connection
 * @buf: a buffer from which to copy the data
 * @len: the length of the data
 *
 * Returns the number of bytes written (could be less than @len), or < 0
 * if there was a failure.
 */
static inline ssize_t tcp_write(tcpconn_t *c, const void *buf, size_t len)
{
	return __tcp_write(c, buf, len, false, false);
}

/**
 * tcp_write_nt - similar with tcp_write, but uses non-temporal memcpy
 */
static inline ssize_t tcp_write_nt(tcpconn_t *c, const void *buf, size_t len)
{
	return __tcp_write(c, buf, len, true, false);
}

/**
 * tcp_write_nt - similar with tcp_write, but keeps polling on waitq
 */
static inline ssize_t tcp_write_poll(tcpconn_t *c, const void *buf, size_t len)
{
	return __tcp_write(c, buf, len, false, true);
}

/**
 * tcp_writev - writes vectored data to a TCP connection
 * @c: the TCP connection
 * @iov: a pointer to the IO vector
 * @iovcnt: the number of vectors in @iov
 *
 * Returns the number of bytes written (could be less than requested), or < 0
 * if there was a failure.
 */
static inline ssize_t tcp_writev(tcpconn_t *c, const struct iovec *iov,
				 int iovcnt)
{
	return __tcp_writev(c, iov, iovcnt, false, false);
}

/**
 * tcp_writev_nt - similar with tcp_writev, but uses non-temporal memcpy
 */
static inline ssize_t tcp_writev_nt(tcpconn_t *c, const struct iovec *iov,
				    int iovcnt)
{
	return __tcp_writev(c, iov, iovcnt, true, false);
}

/**
 * tcp_writev_nt - similar with tcp_writev, but keeps polling on waitq
 */
static inline ssize_t tcp_writev_poll(tcpconn_t *c, const struct iovec *iov,
				    int iovcnt)
{
	return __tcp_writev(c, iov, iovcnt, false, true);
}

/**
 * tcp_read - reads data from a TCP connection
 * @c: the TCP connection
 * @buf: a buffer to store the read data
 * @len: the length of @buf
 *
 * Returns the number of bytes read, 0 if the connection is closed, or < 0
 * if an error occurred.
 */
static inline ssize_t tcp_read(tcpconn_t *c, void *buf, size_t len)
{
	return __tcp_read(c, buf, len, false, false);
}

/**
 * tcp_read_nt - similar with tcp_read, but uses non-temporal memcpy
 */
static inline ssize_t tcp_read_nt(tcpconn_t *c, void *buf, size_t len)
{
	return __tcp_read(c, buf, len, true, false);
}

/**
 * tcp_read_poll - similar with tcp_read, but keeps polling on waitq
 */
static inline ssize_t tcp_read_poll(tcpconn_t *c, void *buf, size_t len)
{
	return __tcp_read(c, buf, len, true, true);
}

/**
 * tcp_readv - reads vectored data from a TCP connection
 * @c: the TCP connection
 * @iov: a pointer to the IO vector
 * @iovcnt: the number of vectors in @iov
 *
 * Returns the number of bytes read, 0 if the connection is closed, or < 0
 * if an error occurred.
 */
static inline ssize_t tcp_readv(tcpconn_t *c, const struct iovec *iov,
				int iovcnt)
{
	return __tcp_readv(c, iov, iovcnt, false, false);
}

/**
 * tcp_readv_nt - similar with tcp_readv, but uses non-temporal memcpy
 */
static inline ssize_t tcp_readv_nt(tcpconn_t *c, const struct iovec *iov,
				   int iovcnt)
{
	return __tcp_readv(c, iov, iovcnt, true, false);
}

/**
 * tcp_readv_poll - similar with tcp_readv, but keeps polling on waitq
 */
static inline ssize_t tcp_readv_poll(tcpconn_t *c, const struct iovec *iov,
				   int iovcnt)
{
	return __tcp_readv(c, iov, iovcnt, true, true);
}

/**
 * tcp_dial - opens a TCP connection, creating a new socket
 * @laddr: the local address
 * @raddr: the remote address
 * @c_out: a pointer to store the new connection
 *
 * Returns 0 if successful, otherwise fail.
 */
static inline int tcp_dial(struct netaddr laddr, struct netaddr raddr,
	                   tcpconn_t **c_out)
{
	return __tcp_dial(laddr, raddr, c_out, IPTOS_DSCP_CS0, false);
}

/**
 * tcp_dial_dscp - similar with tcp_dial, but allows to specify dscp.
 */
static inline int tcp_dial_dscp(struct netaddr laddr, struct netaddr raddr,
		                tcpconn_t **c_out, uint8_t dscp, bool poll)
{
	if (unlikely(dscp > IPTOS_DSCP_MAX)) {
		return -EINVAL;
	}
	return __tcp_dial(laddr, raddr, c_out, dscp, poll);
}

/**
 * tcp_dial_affinity - opens a TCP connection with specific kthread affinity
 * @in: the connection to match to
 * @raddr: the remote address
 * @c_out: a pointer to store the new connection
 *
 * Returns 0 if successful, otherwise fail.
 *
 * Note: in the future this can be better integrated with tcp_dial.
 * for now, it simply wraps it.
 */
static inline int tcp_dial_affinity(uint32_t affinity, struct netaddr raddr,
		                    tcpconn_t **c_out)
{
	return __tcp_dial_affinity(affinity, raddr, c_out, IPTOS_DSCP_CS0);
}

/**
 * tcp_dial_affinity_dscp - similar with tcp_dial_affinity, but allows to
 * specify dscp.
 */
static inline int tcp_dial_affinity_dscp(uint32_t affinity,
					 struct netaddr raddr,
			                 tcpconn_t **c_out, uint8_t dscp)
{
	if (unlikely(dscp > IPTOS_DSCP_MAX)) {
		return -EINVAL;
	}
	return __tcp_dial_affinity(affinity, raddr, c_out, dscp);
}

/**
 * tcp_dial_conn_affinity - opens a TCP connection with matching
 * kthread affinity to another socket
 * @in: the connection to match to
 * @raddr: the remote address
 * @c_out: a pointer to store the new connection
 *
 * Returns 0 if successful, otherwise fail.
 *
 * Note: in the future this can be better integrated with tcp_dial.
 * for now, it simply wraps it.
 */
static inline int tcp_dial_conn_affinity(tcpconn_t *in, struct netaddr raddr,
	                                 tcpconn_t **c_out)
{
	return __tcp_dial_conn_affinity(in, raddr, c_out, IPTOS_DSCP_CS0);
}

/**
 * tcp_dial_conn_affinity_dscp - similar with tcp_dial_conn_affinity,
 * but allows to specify dscp.
 */
static inline int tcp_dial_conn_affinity_dscp(tcpconn_t *in,
					      struct netaddr raddr,
				              tcpconn_t **c_out, uint8_t dscp)
{
	if (unlikely(dscp > IPTOS_DSCP_MAX)) {
		return -EINVAL;
	}
	return __tcp_dial_conn_affinity(in, raddr, c_out, dscp);
}

/**
 * tcp_listen - creates a TCP listening queue for a local address
 * @laddr: the local address to listen on
 * @backlog: the maximum number of unaccepted sockets to queue
 * @q_out: a pointer to store the newly created listening queue
 *
 * Returns 0 if successful, otherwise fails.
 */
static inline int tcp_listen(struct netaddr laddr, int backlog,
			     tcpqueue_t **q_out)
{
	return __tcp_listen(laddr, backlog, q_out, IPTOS_DSCP_CS0);
}

/**
 * tcp_listen_dscp - similar with tcp_listen, but allows to specify dscp.
 */
static inline int tcp_listen_dscp(struct netaddr laddr, int backlog,
			          tcpqueue_t **q_out, uint8_t dscp)
{
	return __tcp_listen(laddr, backlog, q_out, dscp);
}
