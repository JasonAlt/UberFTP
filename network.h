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
#ifndef UBER_NETWORK_H
#define UBER_NETWORK_H

#include <sys/socket.h>

#include "errcode.h"

typedef struct network_handle_t nh_t;

int
net_connected(nh_t * nh);

errcode_t
net_connect(nh_t ** nh, struct sockaddr * sin, socklen_t sin_len);

errcode_t
net_listen(nh_t ** nhp, struct sockaddr * sin, socklen_t sin_len);

errcode_t
net_accept(nh_t * nh, nh_t ** nhp);

void
net_close(nh_t * nh);

void
net_destroy(nh_t * nh);

errcode_t
net_read(nh_t * nh, char * buf, size_t * count, int * eof);

errcode_t
net_write(nh_t * nh, char * buf, size_t count);

errcode_t
net_write_nb(nh_t * nh, char * buf, size_t * count);

errcode_t
net_wait(nh_t * nh1, nh_t * nh2, int timeout);

errcode_t
net_poll(nh_t * nh, int * read, int * write, int timeout);

errcode_t
net_getsockname(nh_t * nh, struct sockaddr * sin, socklen_t sin_len);

errcode_t
net_getpeername(nh_t * nh, struct sockaddr * sin, socklen_t sin_len);

errcode_t
net_translate(char * host, int port, struct addrinfo ** saddr);

#endif /* UBER_NETWORK_H */
