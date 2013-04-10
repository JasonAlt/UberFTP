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

#include <stdlib.h>

#include "settings.h"
#include "errcode.h"
#include "network.h"
#include "output.h"
#include "ftp_s.h"
#include "misc.h"
#include "gsi.h"
#include "ftp.h"

#ifdef DMALLOC
#include "dmalloc.h"
#endif /* DMALLOC */

#define DC_STATE_RD_WR        0x00
#define DC_STATE_ACCEPT       0x01
#define DC_STATE_CONNECT      0x02
#define DC_STATE_ACCEPT_AUTH  0x03
#define DC_STATE_CONNECT_AUTH 0x04

typedef struct _dc_ {
    nh_t * nh;
	gh_t * gh;
    int    state; /* 0 read/write, 1 listen, 2 connect */
	globus_off_t off;
} dc_t;

static errcode_t
_f_s_poll(dch_t * dch)
{
	errcode_t ec   = EC_SUCCESS;
	int       done = 0;
	dc_t    * dc   = (dc_t*)dch->privdata;
	nh_t    * nh   = NULL;

	switch (dc->state)
	{
	case DC_STATE_CONNECT:
		dc->state = DC_STATE_CONNECT_AUTH;
	case DC_STATE_CONNECT_AUTH:
		ec = gsi_dc_auth(&dc->gh, dc->nh, dch->pbsz, dch->dcau, 0, &done);

		if (!ec && done)
			dc->state = DC_STATE_RD_WR;
		break;

	case DC_STATE_ACCEPT:
		ec = net_accept(dc->nh, &nh);
		if (ec || !nh)
			break;

		net_close(dc->nh);
		net_destroy(dc->nh);
		dc->nh = nh;
		dc->state = DC_STATE_ACCEPT_AUTH;
		break;

	case DC_STATE_ACCEPT_AUTH:
		ec = gsi_dc_auth(&dc->gh, dc->nh, dch->pbsz, dch->dcau, 1, &done);

		if (!ec && done)
			dc->state = DC_STATE_RD_WR;
		break;
	}
	return ec;
}


static errcode_t
_f_s_active(dch_t * dch, struct sockaddr * sin, socklen_t sin_len, int scnt)
{
	errcode_t ec = EC_SUCCESS;
	dc_t    * dc = NULL;

	dch->privdata = dc = (dc_t*) malloc(sizeof(dc_t));
	memset(dc, 0, sizeof(dc_t));

	/* Non blocking connect. */
	ec = net_connect(&dc->nh, sin, sin_len);
	if (ec)
	{
		FREE(dch->privdata);
		dch->privdata = NULL;
		return ec;
	}

	dc->state = DC_STATE_CONNECT;
	return ec;
}


static errcode_t
_f_s_passive(dch_t * dch, struct sockaddr * sin, socklen_t sin_len)
{
	errcode_t ec = EC_SUCCESS;
	dc_t    * dc = NULL;

	dch->privdata = dc = (dc_t*) malloc(sizeof(dc_t));
	memset(dc, 0, sizeof(dc_t));

	ec = net_listen(&dc->nh, sin, sin_len);
	if (ec)
	{
		FREE(dch->privdata);
		dch->privdata = NULL;
		return ec;
	}

	dc->state = DC_STATE_ACCEPT;
	return ec;
}

static errcode_t
_f_s_read_ready(dch_t * dch, int * ready)
{
	errcode_t ec  = EC_SUCCESS;
	dc_t    * dc   = (dc_t*)dch->privdata;

	*ready = 0;

	ec = _f_s_poll(dch);
	if (ec || dc->state != DC_STATE_RD_WR)
		return ec;

	if (!gsi_dc_ready(dc->gh, dc->nh, 1))
		ec = gsi_dc_fl_read(dc->gh, dc->nh);
	if (ec)
		return ec;

	if (gsi_dc_ready(dc->gh, dc->nh, 1))
		*ready = 1;
	return ec;
}

static errcode_t
_f_s_read(dch_t         * dch,
          char         ** buf,
          globus_off_t  * off,
          size_t        * len,
          int           * eof)
{
	errcode_t ec  = EC_SUCCESS;
	dc_t    * dc   = (dc_t*)dch->privdata;

	do {
		ec = _f_s_poll(dch);
	} while (!ec && dc->state != DC_STATE_RD_WR);

	if (ec)
		return ec;

	while (!ec && !gsi_dc_ready(dc->gh, dc->nh, 1))
	{
		ec = gsi_dc_fl_read(dc->gh, dc->nh);
	}
	if (ec)
		return ec;

	ec = gsi_dc_read(dc->gh, dc->nh, buf, len, eof);

	*off = dc->off + dch->partial_off;
	dc->off += *len;

	return ec;
}

static errcode_t
_f_s_write_ready(dch_t * dch, int * ready)
{
	errcode_t ec  = EC_SUCCESS;
	dc_t    * dc   = (dc_t*)dch->privdata;

	*ready = 0;
	ec = _f_s_poll(dch);
	if (ec || dc->state != DC_STATE_RD_WR)
		return ec;

	if (!gsi_dc_ready(dc->gh, dc->nh, 0))
		ec = gsi_dc_fl_write(dc->gh, dc->nh);
	if (ec)
		return ec;

	if (gsi_dc_ready(dc->gh, dc->nh, 0))
		*ready = 1;
	return ec;
}

static errcode_t
_f_s_write(dch_t * dch,
           char  * buf,
           globus_off_t    off,
           size_t          len,
           int             eof)
{

	errcode_t ec  = EC_SUCCESS;
	dc_t    * dc   = (dc_t*)dch->privdata;

	ec = _f_s_poll(dch);
	if (ec)
		return ec;

	while (!ec && !gsi_dc_ready(dc->gh, dc->nh, 0))
	{
		ec = gsi_dc_fl_write(dc->gh, dc->nh);
	}

	if (ec)
		return ec;

	return gsi_dc_write(dc->gh, dc->nh, buf, len, eof);
}

static void
_f_s_close(dch_t * dch)
{
	errcode_t ec  = EC_SUCCESS;
	dc_t    * dc  = (dc_t*)dch->privdata;
	int wr = 0;
	int rd = 0;

	if (!dch->privdata)
		return;

	if (dc->state == DC_STATE_CONNECT)
	{
		do {
			rd = wr = 1;
			ec = net_poll(dc->nh, &rd, &wr, -1);
		} while (ec == EC_SUCCESS && !wr && !rd);
		ec_destroy(ec);
	}

	net_destroy(dc->nh);
	gsi_destroy(dc->gh);
	FREE(dc);
	dch->privdata = NULL;
}


const dci_t Ftp_s_dci = {
	_f_s_active,
	_f_s_passive,
	_f_s_read_ready,
	_f_s_read,
	_f_s_write_ready,
	_f_s_write,
	_f_s_close,
};

