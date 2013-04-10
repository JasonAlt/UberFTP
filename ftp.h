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
#ifndef UBER_FTP_H
#define UBER_FTP_H

#include <sys/socket.h>
#include <globus_common.h>

#include "linterface.h"
#include "network.h"
#include "errcode.h"

extern const Linterface_t FtpInterface;

typedef struct _dch_ dch_t;

typedef struct _dci_h {
	errcode_t (*active)(dch_t *, struct sockaddr *, socklen_t, int scnt);
	errcode_t (*passive)(dch_t *, struct sockaddr * sin, socklen_t sin_len);
	errcode_t (*read_ready) (dch_t * dch, int * ready);
	errcode_t (*read)(dch_t *, 
	                  char         ** buf,
	                  globus_off_t  * off, 
	                  globus_size_t * len,
	                  int * eof);
	errcode_t (*write_ready) (dch_t * dch, int * ready);
	errcode_t (*write)(dch_t *, 
	                   char        * buf,
	                   globus_off_t  off, 
	                   globus_size_t len,
	                   int eof);
	void (*close)(dch_t *);
} dci_t;

/* Handle for all data channels. */
struct _dch_ {
	void   * privdata;
	dci_t    dci;
	int      pbsz; /* Protection buffer size */
	int      dcau; /* 0 no, 1 yes. */ /* Use s_dcau() for settings. */
	globus_off_t partial_off;
};

#endif /* UBER_FTP_H */
