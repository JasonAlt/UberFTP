/*
 * University of Illinois/NCSA Open Source License
 *
 * Copyright © 2003-2010 NCSA.  All rights reserved.
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
net_connect(nh_t ** nhp,  struct sockaddr_in * sin)
{
	int             rval   = 0;
	int             flags  = 0;
	int             wsize  = 0;
	nh_t          * nh     = NULL;
	errcode_t       ec     = EC_SUCCESS;
	unsigned short  port   = 0;
	unsigned short  minsrc = s_minsrc();
	unsigned short  maxsrc = s_maxsrc();
	char cname[INET_ADDRSTRLEN];
	struct sockaddr_in bindsin;

	nh = *nhp = (nh_t *) malloc(sizeof(nh_t));

	memset(nh, 0, sizeof(nh_t));

	nh->fd = socket(AF_INET, SOCK_STREAM, 0);
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

	flags = fcntl(nh->fd, F_GETFL, 0);
	if (flags < 0)
	{
		ec = ec_create(EC_GSI_SUCCESS,
		               EC_GSI_SUCCESS,
		               "Failed to get socket flags: %s",
		               strerror(errno));
		goto cleanup;
	}

	rval = fcntl(nh->fd, F_SETFL, flags|O_NONBLOCK);
	if (rval < 0)
	{
		ec = ec_create(EC_GSI_SUCCESS,
		               EC_GSI_SUCCESS,
		               "Failed to set O_NONBLOCK on socket : %s",
		               strerror(errno));
		goto cleanup;
	}

	rval = 1;
	setsockopt(nh->fd, SOL_SOCKET, SO_REUSEADDR, (char *)&rval, sizeof(rval));

	if (minsrc || maxsrc)
	{
		memset(&bindsin, 0, sizeof(struct sockaddr_in));
		bindsin.sin_family = AF_INET;
		bindsin.sin_addr.s_addr = INADDR_ANY;
		for (port = minsrc; port <= maxsrc; port++)
		{
			bindsin.sin_port = htons(port);
			rval = bind(nh->fd, (struct sockaddr *)&bindsin, sizeof(bindsin));
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

		if (port > maxsrc)
		{
			ec = ec_create(EC_GSI_SUCCESS,
			               EC_GSI_SUCCESS,
			               "No available ports in range %hu-%hu",
			               minsrc,
			               maxsrc);
			goto cleanup;
		}
	}

	rval = connect(nh->fd, 
	               (struct sockaddr *)sin, 
	               sizeof(struct sockaddr_in));
	if (rval && errno != EINPROGRESS)
	{
		ec = ec_create(
		             EC_GSI_SUCCESS,
		             EC_GSI_SUCCESS,
		             "Failed to connect to %s port %d: %s",
		             inet_ntop(AF_INET, &sin->sin_addr, cname, INET_ADDRSTRLEN),
					 ntohs(sin->sin_port),
		             strerror(errno));
		goto cleanup;
	}

	nh->state = NET_STATE_CONNECTED;
	if (rval && errno == EINPROGRESS)
		nh->state = NET_STATE_CONNECTING;

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
net_listen(nh_t ** nhp, struct sockaddr_in * sin)
{
	int             rval    = 0;
	int             flags   = 0;
	int             wsize   = 0;
	nh_t          * nh      = NULL;
	errcode_t       ec      = EC_SUCCESS;
	unsigned short  port    = 0;
	unsigned short  minport = s_minport();
	unsigned short  maxport = s_maxport();
	struct sockaddr_in bindsin;

	nh = *nhp = (nh_t *) malloc(sizeof(nh_t));
	memset(nh, 0, sizeof(nh_t));

	nh->fd = socket(AF_INET, SOCK_STREAM, 0);
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

	flags = fcntl(nh->fd, F_GETFL, 0);
	if (flags < 0)
	{
		ec = ec_create(EC_GSI_SUCCESS,
		               EC_GSI_SUCCESS,
		               "Failed to get socket flags: %s",
		               strerror(errno));
		goto cleanup;
	}

	rval = fcntl(nh->fd, F_SETFL, flags|O_NONBLOCK);
	if (rval < 0)
	{
		ec = ec_create(EC_GSI_SUCCESS,
		               EC_GSI_SUCCESS,
		               "Failed to set O_NONBLOCK on socket : %s",
		               strerror(errno));
		goto cleanup;
	}

	rval = 1;
	setsockopt(nh->fd, SOL_SOCKET, SO_REUSEADDR, (char *)&rval, sizeof(rval));

	if (minport || maxport)
	{
		memcpy(&bindsin, sin, sizeof(struct sockaddr_in));
		for (port = minport; port <= maxport; port++)
		{
			bindsin.sin_port = htons(port);
			rval = bind(nh->fd, (struct sockaddr *)&bindsin, sizeof(bindsin));
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
		rval = bind(nh->fd, (struct sockaddr*)sin, sizeof(struct sockaddr_in));
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
	ec = net_getsockname(nh, sin);

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
	int rval = 0;

	*nhp = NULL;
	rval = accept(nh->fd, NULL, NULL);

	if (rval == -1 && (errno ==  EAGAIN || errno == EWOULDBLOCK))
		return EC_SUCCESS;

	if (rval < 0)
		return ec_create(EC_GSI_SUCCESS,
		                 EC_GSI_SUCCESS,
		                 "accept() failed: %s",
		                 strerror(errno));

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
net_getsockname(nh_t * nh, struct sockaddr_in * sin)
{
	int rval = 0;
	socklen_t len = sizeof(struct sockaddr_in);

	rval = getsockname(nh->fd, (struct sockaddr*)sin, &len);
	if (rval < 0)
		return ec_create(EC_GSI_SUCCESS,
		                 EC_GSI_SUCCESS,
		                 "getsockname failed: %s",
		                 strerror(errno));

	return EC_SUCCESS;
}

errcode_t
net_translate(char * host, int port, struct sockaddr_in * sin)
{
	int rval  = 0;
	errcode_t ec = EC_SUCCESS;
	struct addrinfo hints;
	struct addrinfo * res = NULL;

    /* Get the connection information. */
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

	rval = getaddrinfo(host, NULL, &hints, &res);
	if (rval)
	{
		ec = ec_create(EC_GSI_SUCCESS,
		               EC_GSI_SUCCESS,
		               "Could not resolve the IP address for %s: %s",
		               host,
		               gai_strerror(rval));
		goto cleanup;
	}

	memcpy(sin, res->ai_addr, sizeof(struct sockaddr_in));
	sin->sin_port = htons(port);

cleanup:
	if (res)
		freeaddrinfo(res);

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

