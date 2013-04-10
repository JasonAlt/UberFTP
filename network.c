/*
 * University of Illinois/NCSA Open Source License
 *
 * Copyright © 2003-2012 NCSA.  All rights reserved.
 *
 * Developed by:
 *
 * Storage Enabling Technologies (SET)
 *
 * Nation Center for Supercomputing Applications (NCSA)
 *
 * http://dims.ncsa.uiuc.edu/set/uberftp
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the .Software.),
 * to deal with the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 *    + Redistributions of source code must retain the above copyright notice,
 *      this list of conditions and the following disclaimers.
 *
 *    + Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimers in the
 *      documentation and/or other materials provided with the distribution.
 *
 *    + Neither the names of SET, NCSA
 *      nor the names of its contributors may be used to endorse or promote
 *      products derived from this Software without specific prior written
 *      permission.
 *
 * THE SOFTWARE IS PROVIDED .AS IS., WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE CONTRIBUTORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS WITH THE SOFTWARE.
 */
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/poll.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <netdb.h>
#include <fcntl.h>

#include "settings.h"
#include "errcode.h"
#include "network.h"
#include "misc.h"

#ifdef DMALLOC
#include "dmalloc.h"
#endif /* DMALLOC */

#define NET_STATE_CLOSED     0x00
#define NET_STATE_CLOSING    0x01
#define NET_STATE_CONNECTING 0x02
#define NET_STATE_CONNECTED  0x03
#define NET_STATE_LISTENING  0x04

struct network_handle_t
{
	int fd;
	int state;
};

static errcode_t _net_wait_ready(nh_t * nh, int read, int write, int except);

static errcode_t
_net_set_nonblocking(int fd)
{
	int rval  = 0;
	int flags = 0;

	flags = fcntl(fd, F_GETFL, 0);
	if (flags < 0)
	{
		return ec_create(EC_GSI_SUCCESS,
		                 EC_GSI_SUCCESS,
		                 "Failed to get socket flags: %s",
		                 strerror(errno));
	}

	rval = fcntl(fd, F_SETFL, flags|O_NONBLOCK);
	if (rval < 0)
	{
		return ec_create(EC_GSI_SUCCESS,
		                 EC_GSI_SUCCESS,
		                 "Failed to set O_NONBLOCK on socket : %s",
		                 strerror(errno));
	}

	return EC_SUCCESS;
}

int
net_connected(nh_t * nh)
{
	if (!nh)
		return 0;

	if (nh->fd == -1)
		return 0;

	if (nh->state == NET_STATE_CLOSED)
		return 0;

	return 1;
}

errcode_t
net_connect(nh_t ** nhp,  struct sockaddr * sin, socklen_t sin_len)
{
	int             rval            = 0;
	int             wsize           = 0;
	int             have_port_range = 0;
	nh_t          * nh              = NULL;
	errcode_t       ec              = EC_SUCCESS;
	unsigned short  port            = 0;
	unsigned short  minsrc          = s_minsrc();
	unsigned short  maxsrc          = s_maxsrc();
	char cname[INET6_ADDRSTRLEN];
	struct sockaddr_storage bindsinst;
	struct sockaddr_in  *bindsin4;
	struct sockaddr_in6 *bindsin6;
	void          * err_addr;
	in_port_t       err_port;

	/* Get our net handle pointer. */
	nh = *nhp = (nh_t *) malloc(sizeof(nh_t));

	/* Initialize our net handle. */
	memset(nh, 0, sizeof(nh_t));
	nh->fd = -1;

	/* Determine if we have a valid port range. */
	if (minsrc && maxsrc && minsrc < maxsrc)
	{
		/* Indicate that we have a valid range. */
		have_port_range = 1;

		/* Initialize port to the min source port. */
		port = minsrc;
	}

	do
	{
		/* After our first iteration of this loop, this socket may be open. */
		if (nh->fd != -1)
			close(nh->fd);

		/* Create our socket. */
		if (sin->sa_family == AF_INET6)
			nh->fd = socket(PF_INET6, SOCK_STREAM, 0);
		else    nh->fd = socket(PF_INET , SOCK_STREAM, 0);
		if (nh->fd == -1)
		{
			ec = ec_create(EC_GSI_SUCCESS,
			               EC_GSI_SUCCESS,
			               "socket() failed: %s",
			               strerror(errno));
			goto cleanup;
		}

		/* Set our TCP buffer write sisze. */
		wsize = s_tcpbuf();
		if (wsize)
		{
			setsockopt(nh->fd, 
			           SOL_SOCKET, 
			           SO_SNDBUF, 
			           (char *) &wsize, 
			           sizeof(wsize));
			setsockopt(nh->fd, 
			           SOL_SOCKET, 
			           SO_RCVBUF, 
			           (char *) &wsize, 
			           sizeof(wsize));

		}

		/* Set the socket to non blocking. */
		ec = _net_set_nonblocking(nh->fd);
		if (ec != EC_SUCCESS)
			goto cleanup;

		/* Indicate that the socket is to be reused. */
		rval = 1;
		setsockopt(nh->fd, SOL_SOCKET, SO_REUSEADDR, (char *)&rval, sizeof(rval));

		/* If we have port ranges... */
		if (have_port_range)
		{
			/* Set us up for the bind. */
			memset(&bindsinst, 0, sizeof(struct sockaddr_storage));
			bindsin4 = (struct sockaddr_in *)&bindsinst;
			bindsin6 = (struct sockaddr_in6 *)&bindsinst;
			bindsinst.ss_family = AF_INET;
			switch(sin->sa_family)
			{
			 case AF_INET:
				bindsin4->sin_addr.s_addr = INADDR_ANY;
				break;
			 case AF_INET6:
				bindsin6->sin6_addr = in6addr_any;
				break;
			 default:
				break;
			}

			/* Bind. */
			if (bindsinst.ss_family == AF_INET)
				bindsin4->sin_port = htons(port);
			else if (bindsinst.ss_family == AF_INET6)
				bindsin6->sin6_port = htons(port);

			rval = bind(nh->fd, (struct sockaddr *)&bindsinst, sin_len);

			if (rval)
			{
				/* Destroy any previous error. */
				ec_destroy(ec);
				/* Record this error. */
				ec = ec_create(EC_GSI_SUCCESS,
				               EC_GSI_SUCCESS,
				               "Failed to bind socket : %s",
				               strerror(errno));

				/* Retry. */
				continue;
			}
		}

		/* Connect. */
		rval = connect(nh->fd, sin, sin_len);

		if (rval && errno != EINPROGRESS)
		{
			/* Destroy any previous error. */
			ec_destroy(ec);
			/* Record this error. */
			switch(sin->sa_family)
			{
			 case AF_INET:
				err_addr = (void *)(&(((struct sockaddr_in *)(sin))->sin_addr));
				err_port = ntohs(((struct sockaddr_in *)(sin))->sin_port);
				break;
			 case AF_INET6:
				err_addr = (void *)(&(((struct sockaddr_in6 *)(sin))->sin6_addr));
				err_port = ntohs(((struct sockaddr_in6 *)(sin))->sin6_port);
				break;
			 default:
				err_addr = NULL;
				err_port = 0;
			}

			ec = ec_create(
			             EC_GSI_SUCCESS,
			             EC_GSI_SUCCESS,
			             "Failed to connect to %s port %d: %s",
			             inet_ntop(AF_INET, err_addr, cname, sizeof(cname)),
						err_port,
						strerror(errno));

			/* Retry. */
			continue;
		}

		/*
		 * Success!
		 */

		/* Destroy any previously recorded error. */
		ec_destroy(ec);

		/* Indicate success. */
		ec = EC_SUCCESS;

		/* Exit this loop. */
		break;
	} while (have_port_range && ++port < maxsrc);

	if (ec == EC_SUCCESS)
	{
		nh->state = NET_STATE_CONNECTED;
		if (rval && errno == EINPROGRESS)
			nh->state = NET_STATE_CONNECTING;
	}

cleanup:

	if (ec != EC_SUCCESS)
	{
		if (nh->fd != -1)
			close(nh->fd);
		FREE(nh);
		*nhp = NULL;
	}
	return ec;
}

errcode_t
net_listen(nh_t ** nhp, struct sockaddr * sin, socklen_t sin_len)
{
	int             rval    = 0;
	int             wsize   = 0;
	nh_t          * nh      = NULL;
	errcode_t       ec      = EC_SUCCESS;
	unsigned short  port    = 0;
	unsigned short  minport = s_minport();
	unsigned short  maxport = s_maxport();
	struct sockaddr_storage bindsinst;
	struct sockaddr_in  *bindsin4;
	struct sockaddr_in6 *bindsin6;

	nh = *nhp = (nh_t *) malloc(sizeof(nh_t));
	memset(nh, 0, sizeof(nh_t));

	if (sin->sa_family == AF_INET6)
		nh->fd = socket(PF_INET6, SOCK_STREAM, 0);
	else	nh->fd = socket(PF_INET , SOCK_STREAM, 0);
	if (nh->fd == -1)
	{
		ec = ec_create(EC_GSI_SUCCESS,
		               EC_GSI_SUCCESS,
		               "socket() failed: %s",
		               strerror(errno));
		goto cleanup;
	}

	wsize = s_tcpbuf();
	if (wsize)
	{
		setsockopt(nh->fd, 
		           SOL_SOCKET, 
		           SO_SNDBUF, 
		           (char *) &wsize, 
		           sizeof(wsize));
		setsockopt(nh->fd, 
		           SOL_SOCKET, 
		           SO_RCVBUF, 
		           (char *) &wsize, 
		           sizeof(wsize));

	}

	ec = _net_set_nonblocking(nh->fd);
	if (ec != EC_SUCCESS)
		goto cleanup;

	rval = 1;
	setsockopt(nh->fd, SOL_SOCKET, SO_REUSEADDR, (char *)&rval, sizeof(rval));

	if (minport || maxport)
	{
		memcpy(&bindsinst, sin, sizeof(struct sockaddr_storage));
		bindsin4 = (struct sockaddr_in *)&bindsinst;
		bindsin6 = (struct sockaddr_in6 *)&bindsinst;
		for (port = minport; port <= maxport; port++)
		{
			if (bindsinst.ss_family == AF_INET) 
				bindsin4->sin_port = htons(port);
			else if (bindsinst.ss_family == AF_INET6)
				bindsin6->sin6_port = htons(port);

			rval = bind(nh->fd, (struct sockaddr *)&bindsinst, sin_len);
			if (!rval)
				break;

			switch (errno)
			{
			case EADDRINUSE:
			case EACCES:
				break;

			default:
				ec = ec_create(EC_GSI_SUCCESS,
				               EC_GSI_SUCCESS,
				               "Failed to bind socket : %s",
				               strerror(errno));
				goto cleanup;
			}
		}

		if (port > maxport)
		{
			ec = ec_create(EC_GSI_SUCCESS,
			               EC_GSI_SUCCESS,
			               "No available ports in range %hu-%hu",
			               minport,
			               maxport);
			goto cleanup;
		}
	} else
	{
		rval = bind(nh->fd, sin, sin_len);
		if (rval < 0)
		{
			ec = ec_create(EC_GSI_SUCCESS,
			               EC_GSI_SUCCESS,
			               "Failed to bind socket : %s",
			               strerror(errno));
			goto cleanup;
		}
	}

	rval = listen(nh->fd, 15);
	if (rval < 0)
	{
		ec = ec_create(EC_GSI_SUCCESS,
		               EC_GSI_SUCCESS,
		               "Failed to set listen on socket : %s",
		               strerror(errno));
		goto cleanup;
	}

	/* Update the sock structure */
	ec = net_getsockname(nh, sin, sin_len);

	nh->state = NET_STATE_LISTENING;
cleanup:
	if (ec != EC_SUCCESS)
	{
		if (nh->fd != -1)
			close(nh->fd);
		FREE(nh);
		*nhp = NULL;
	}
	return ec;
}

errcode_t
net_accept(nh_t * nh, nh_t ** nhp)
{
	int       rval = 0;
	errcode_t ec   = EC_SUCCESS;

	*nhp = NULL;
	rval = accept(nh->fd, NULL, NULL);

	if (rval == -1 && (errno ==  EAGAIN || errno == EWOULDBLOCK))
		return EC_SUCCESS;

	if (rval < 0)
		return ec_create(EC_GSI_SUCCESS,
		                 EC_GSI_SUCCESS,
		                 "accept() failed: %s",
		                 strerror(errno));

	ec = _net_set_nonblocking(rval);
	if (ec != EC_SUCCESS)
	{
		close(rval);
		return ec;
	}

	*nhp = (nh_t *) malloc(sizeof(nh_t));
	(*nhp)->fd = rval;
	(*nhp)->state = NET_STATE_CONNECTED;

	return EC_SUCCESS;
}

void
net_close(nh_t * nh)
{
	if (!nh)
		return;

	if (nh->state == NET_STATE_CLOSED)
		return;

	nh->state = NET_STATE_CLOSED;
	close(nh->fd);
	return;
}

void
net_destroy(nh_t * nh)
{
	if (nh)
	{
		net_close(nh);
		FREE(nh);
	}
}

errcode_t
net_read(nh_t * nh, char * buf, size_t * count, int * eof) 
{
	errcode_t ec = EC_SUCCESS;
	ssize_t cnt = 0;

	*eof   = 0;

/*
	if (nh->state == NET_STATE_CONNECTING)
	{
		ec = _net_wait_ready(nh, 1, 0, 0);
		if (ec != EC_SUCCESS)
			return ec;
	}
*/

	nh->state = NET_STATE_CONNECTED;

	cnt = read(nh->fd, buf, *count);

	if (cnt == -1 && (errno == EAGAIN || errno == EINTR))
	{
		*count = 0;
		return ec;
	}
	
	if (cnt == -1)
		return ec_create(EC_GSI_SUCCESS,
		                 EC_GSI_SUCCESS,
		                 "Failed to read(): %s",
		                 strerror(errno));

	*count = cnt;
	if (cnt == 0)
	{
		*eof = 1;
		nh->state = NET_STATE_CLOSING;
	}

	return ec;
}

errcode_t
net_write(nh_t * nh, char * buf, size_t count)
{
	errcode_t ec = EC_SUCCESS;
	ssize_t cnt = 0;
	size_t  off = 0;

	while (count > off)
	{
		ec = _net_wait_ready(nh, 0, 1, 0);
		if (ec != EC_SUCCESS)
			return ec;

		cnt = write(nh->fd, buf + off, count - off);

		if (cnt == -1 && errno != EINTR && errno != EAGAIN)
		{
			net_close(nh);
			return ec_create(EC_GSI_SUCCESS,
			                 EC_GSI_SUCCESS,
			                 "write() failed: %s",
			                 strerror(errno));
		}

		if (cnt > 0)
			off += cnt;
	}

	return ec;
}

errcode_t
net_write_nb(nh_t * nh, char * buf, size_t * count)
{
	errcode_t ec  = EC_SUCCESS;
	ssize_t   cnt = 0;

	cnt = write(nh->fd, buf, *count);
	if (cnt == -1 && errno != EINTR && errno != EAGAIN)
	{
		return ec_create(EC_GSI_SUCCESS,
		                 EC_GSI_SUCCESS,
		                 "write() failed: %s",
		                 strerror(errno));
	}

	if (cnt > 0)
		*count -= cnt;

	return ec;
}

errcode_t
net_wait(nh_t * nh1, nh_t * nh2, int timeout)
{
	int rval  = 0;
	int count = 0;
	struct pollfd ufd[2];

	if (nh1->state != NET_STATE_CLOSED && nh1->state != NET_STATE_CLOSING)
	{
		ufd[count].fd        = nh1->fd;
		ufd[count].events    = POLLIN;
		ufd[count++].revents = 0;
	}

	if (nh2->state != NET_STATE_CLOSED && nh2->state != NET_STATE_CLOSING)
	{
		ufd[count].fd        = nh2->fd;
		ufd[count].events    = POLLIN;
		ufd[count++].revents = 0;
	}

	/* If both handles are closed... */
	if (count == 0)
		return EC_SUCCESS;

	do {
		rval = poll(ufd, count, timeout);
	} while (rval == -1 && errno == EINTR);

	if (rval == -1)
		return ec_create(EC_GSI_SUCCESS,
		                 EC_GSI_SUCCESS,
		                 "poll() failed: %s",
		                 strerror(errno));

	return EC_SUCCESS;
}

errcode_t
net_poll(nh_t * nh, int * read, int * write, int timeout)
{
	int rval = 0;
	struct pollfd ufd;

	if (nh->state == NET_STATE_CLOSED || nh->state == NET_STATE_CLOSING)
	{
		if (read)
			*read = 0;
		if (write)
			*write = 0;

		return EC_SUCCESS;
	}

	ufd.fd = nh->fd;
	ufd.events  = 0;
	ufd.revents = 0;

	if (read)
		ufd.events |= POLLIN;

	if (write)
		ufd.events |= POLLOUT;

	do {
		rval = poll(&ufd, 1, timeout);
	} while (rval == -1 && errno == EINTR);

	if (rval == -1)
		return ec_create(EC_GSI_SUCCESS,
		                 EC_GSI_SUCCESS,
		                 "poll() failed: %s",
		                 strerror(errno));

	if (read)
		*read = 0;
	if (write)
		*write = 0;

	if (read && (ufd.revents & POLLIN))
		*read = 1;
	if (write && (ufd.revents & POLLOUT))
		*write = 1;

	return EC_SUCCESS;
}

errcode_t
net_getsockname(nh_t * nh, struct sockaddr * sin, socklen_t sin_len)
{
	int rval = 0;

	rval = getsockname(nh->fd, sin, &sin_len);
	if (rval < 0)
		return ec_create(EC_GSI_SUCCESS,
		                 EC_GSI_SUCCESS,
		                 "getsockname failed: %s",
		                 strerror(errno));

	return EC_SUCCESS;
}

errcode_t
net_getpeername(nh_t * nh, struct sockaddr * sin, socklen_t sin_len)
{
	int rval = 0;

	rval = getpeername(nh->fd, sin, &sin_len);
	if (rval < 0)
		return ec_create(EC_GSI_SUCCESS,
		                 EC_GSI_SUCCESS,
		                 "getpeername failed: %s",
		                 strerror(errno));

	return EC_SUCCESS;
}

errcode_t
net_translate(char * host, int port, struct addrinfo ** saddr)
{
	int rval  = 0;
	errcode_t ec = EC_SUCCESS;
	struct addrinfo hints;
	char cport[8];

	/* Get the connection information. */
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family   = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	snprintf(cport, sizeof(cport), "%5.5d", port);

	rval = getaddrinfo(host, cport, &hints, saddr);
	if (rval || (*saddr == NULL))
	{
		ec = ec_create(EC_GSI_SUCCESS,
		               EC_GSI_SUCCESS,
		               "Could not resolve the IP address for %s: %s",
		               host,
		               gai_strerror(rval));
	}

	return ec;
}

static errcode_t
_net_wait_ready(nh_t * nh, int read, int write, int except)
{
	int rval = 0;
	fd_set rset;
	fd_set wset;
	fd_set eset;

	do {
		FD_ZERO(&rset);
		FD_ZERO(&wset);
		FD_ZERO(&eset);

		FD_SET(nh->fd, &rset);
		FD_SET(nh->fd, &wset);
		FD_SET(nh->fd, &eset);

		rval = select(nh->fd+1, 
		              read   ? &rset : NULL, 
		              write  ? &wset : NULL, 
		              except ? &eset : NULL, 
		              NULL);
	} while (rval == 0 || (rval == -1 && errno == EINTR));

	if (rval == -1)
		return ec_create(EC_GSI_SUCCESS,
		                 EC_GSI_SUCCESS,
		                 "select() failed: %s",
		                 strerror(errno));

	return EC_SUCCESS;
}

