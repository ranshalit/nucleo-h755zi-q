/* SPDX-License-Identifier: Apache-2.0 */
/* Maps the Zephyr socket API onto POSIX sockets for host tests. */
#ifndef TEST_SHIM_ZEPHYR_SOCKET_H_
#define TEST_SHIM_ZEPHYR_SOCKET_H_

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define zsock_pollfd pollfd
#define zsock_timeval timeval
#define ZSOCK_POLLIN POLLIN
#define ZSOCK_POLLOUT POLLOUT
#define ZSOCK_POLLERR POLLERR
#define ZSOCK_POLLHUP POLLHUP
#define ZSOCK_POLLNVAL POLLNVAL
#define ZSOCK_MSG_DONTWAIT MSG_DONTWAIT
#define ZSOCK_MSG_PEEK MSG_PEEK

#define zsock_socket socket
#define zsock_bind bind
#define zsock_accept accept
#define zsock_setsockopt setsockopt
#define zsock_recv recv
#define zsock_close close
#define zsock_send(fd, buf, len, flags) send((fd), (buf), (len), (flags) | MSG_NOSIGNAL)
/* Hooks let the test observe the listener and poll calls. */
#define zsock_listen test_zsock_listen
#define zsock_poll test_zsock_poll

int test_zsock_listen(int fd, int backlog);
int test_zsock_poll(struct pollfd *fds, int nfds, int timeout);

#endif /* TEST_SHIM_ZEPHYR_SOCKET_H_ */
