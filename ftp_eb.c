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

/*
 * NOTE ABOUT FILE/TRANSFER OFFSETS IN EXTENDED BLOCK MODE
 *
 * The offset used for sending/receiving buffers in extended block mode
 * should be the transfer offset (starting at 0 regardless of starting
 * file offset). If the transfer is a restart, the first offset received
 * will not be zero but only because it is a restart!
 * 
 *   STOR <file>             - Offset 0, 1, 2
 *   ESTO <off> <len> <file> - Offset 0, 1, 2 (receiver should add <off>)
 *   REST <off1>-<off2>      - Offset 0-off1, off2, ...
 *   REST 0-<off1>           - Offset off1, off1+1, ...
 *
 *  This was determined by watching globus-url-copy and globus-gridftp-server
 *  in GT 5.2.3rc2.
 */

#include <stdlib.h>

#include "settings.h"
#include "errcode.h"
#include "network.h"
#include "output.h"
#include "ftp_eb.h"
#include "misc.h"
#include "gsi.h"
#include "ftp.h"

#ifdef DMALLOC
#include "dmalloc.h"
#endif /* DMALLOC */

#define DC_STATE_ACCEPT        0x00
#define DC_STATE_CONNECT       0x01
#define DC_STATE_ACCEPT_AUTH   0x02
#define DC_STATE_CONNECT_AUTH  0x03
#define DC_STATE_HEADER_PULLUP 0x04
#define DC_STATE_READ_READY    0x05

#define DC_STATE_READY         0x10
#define DC_STATE_PUSH_HEADER   0x20
#define DC_STATE_FLUSH_HEADER  0x30
#define DC_STATE_PUSH_DATA     0x40
#define DC_STATE_FLUSH_DATA    0x50
#define DC_STATE_PUSH_EOF      0x60
#define DC_STATE_FLUSH_EOF     0x70
#define DC_STATE_PUSH_EOD      0x80
#define DC_STATE_FLUSH_EOD     0x90
#define DC_STATE_EOD           0xA0

#define EB_HEADER_LEN (1+8+8)

typedef struct _dc_ {
    nh_t * nh;
	gh_t * gh;
	char * buf;
	int    buflen;
    int    state; /* 0 read/write, 1 listen, 2 connect */
	int    eod;
	int    eof;
	globus_off_t off;
	globus_off_t count;
} dc_t;

typedef struct {
	dc_t * dcs;
	int    dccnt;
	int    eods;
	int    eeods;
} ebpd_t;

static errcode_t
_f_eb_read_pullup(dc_t * dc);

static errcode_t
_f_eb_header_pullup(ebpd_t * ebpd, dc_t * dc);

static errcode_t
_f_eb_poll(dch_t * dch);

static char *
_f_eb_header(char desc, globus_off_t count, globus_off_t off);

static errcode_t
_f_eb_push_header(ebpd_t * ebpd, dc_t * dc);

static errcode_t
_f_eb_push_eod(ebpd_t * ebpd, dc_t * dc);

static errcode_t
_f_eb_push_eof(ebpd_t * ebpd, dc_t * dc);

static errcode_t
_f_eb_push_data(ebpd_t * ebpd, dc_t * dc);

static errcode_t
_f_eb_active(dch_t * dch, struct sockaddr * sin, socklen_t sin_len, int scnt)
{
	errcode_t ec   = EC_SUCCESS;
	ebpd_t  * ebpd = NULL;
	int       s    = 0;
	int       p    = 0;

	dch->privdata = ebpd = (ebpd_t*) malloc(sizeof(ebpd_t));
	memset(ebpd, 0, sizeof(ebpd_t));

	ebpd->dcs = (dc_t*) malloc(sizeof(dc_t) * (scnt * s_parallel()));
	memset(ebpd->dcs, 0, sizeof(dc_t) * (scnt * s_parallel()));

	for (s = 0; s < scnt; s++)
	{
		for (p = 0; p < s_parallel(); p++)
		{
			/* Non blocking connect. */
			ec = net_connect(&ebpd->dcs[ebpd->dccnt].nh, &sin[s], sin_len);
			if (ec)
				return ec;

			ebpd->dcs[ebpd->dccnt].state = DC_STATE_CONNECT;
			ebpd->dccnt++;
		}
	}
	ebpd->eeods = ebpd->dccnt;
	return ec;
}

static errcode_t
_f_eb_passive(dch_t * dch, struct sockaddr * sin, socklen_t sin_len)
{
	errcode_t ec   = EC_SUCCESS;
	ebpd_t  * ebpd = NULL;

	dch->privdata = ebpd = (ebpd_t*) malloc(sizeof(ebpd_t));
	memset(ebpd, 0, sizeof(ebpd_t));

	ebpd->dcs = (dc_t*) malloc(sizeof(dc_t));
	memset(ebpd->dcs, 0, sizeof(dc_t));

	ec = net_listen(&ebpd->dcs[0].nh, sin, sin_len);
	if (ec)
		return ec;

	ebpd->dcs[0].state = DC_STATE_ACCEPT;
	ebpd->dccnt++;
	return ec;
}

static errcode_t
_f_eb_read_ready(dch_t * dch, int * ready)
{
	int       i    = 0;
	int       eods = 0;
	ebpd_t  * ebpd = (ebpd_t *) dch->privdata;
	errcode_t ec   = EC_SUCCESS;

	/* Initialize the return. */
	*ready = 0;

	/* 'poll' to push each channel forward. */
	ec = _f_eb_poll(dch);
	if (ec)
		return ec;

	/*
	 * We are 'read ready' if any channel is in READ_READY and has a buflen > 0
	 * or all channels are at eod w/ count = 0.
	 */

	for (i = 0; i < ebpd->dccnt; i++)
	{
		/* If the channel is at eod w/ no data to read... */
		if (ebpd->dcs[i].eod && ebpd->dcs[i].count == 0)
			eods++;

		if (ebpd->dcs[i].state == DC_STATE_READ_READY)
		{
			if (ebpd->dcs[i].buflen > 0)
			{
				*ready = 1;
				return EC_SUCCESS;
			}
		}
	}

	/* If no one was ready... */
	if (*ready == 0)
	{
		/* If we received the EOF header... */
		if (ebpd->eeods > 0)
		{
			/* If every channel has received an EOD... */
			if (ebpd->eeods == eods)
				*ready = 1;
		}
	}

	return EC_SUCCESS;
}

static errcode_t
_f_eb_read(dch_t        * dch,
          char         ** buf,
          globus_off_t  * off,
          size_t        * len,
          int           * eof)
{
	int         i    = 0;
	dc_t      * dc   = NULL;
	ebpd_t    * ebpd = (ebpd_t *) dch->privdata;
	errcode_t   ec   = EC_SUCCESS;

	*buf = NULL;
	*len = 0;
	*off = 0;
	*eof = 0;

	while (!dc)
	{
		ec = _f_eb_poll(dch);
		if (ec)
			return ec;

		for (i = 0; i < ebpd->dccnt; i++)
		{
			if (ebpd->dcs[i].state == DC_STATE_READ_READY)
			{
				/* If this channel is at eod w/ no data to follow... */
				if (ebpd->dcs[i].eod && ebpd->dcs[i].count == 0)
				{
					/* Mark the channel as EOD. */
					ebpd->dcs[i].state = DC_STATE_EOD;
					/* Increment the eod count. */
					ebpd->eods++;

					/* Continue to the next channel. */
					continue;
				}

				/* If this channel has data in its buffer... */
				if (ebpd->dcs[i].buflen > 0)
				{
					/* Find the larget buffer to read. */
					if (!dc || dc->buflen < ebpd->dcs[i].buflen)
						dc = &ebpd->dcs[i];
				}
			}
		}

		/* If we could not find a channel to read... */
		if (!dc)
		{
			/* If we have received the EOF header w/ the expected eod count...*/
			if (ebpd->eeods > 0)
			{
				/* If all channels are now at EOD... */
				if (ebpd->eods == ebpd->eeods)
				{
					/* Indicate EOF to the caller. */
					*eof = 1;
					/* Return success. */
					return EC_SUCCESS;
				}
			}
		}
	}

	/*
	 * We have a channel w/ a buffer!
	 *
	 * 4 scenerios here:
	 *   1) We have more data than this block holds. Copy it out and pull the
	 *      next header.
	 *   2) We have less data than this block holds. Copy it out and read more
	 *      data.
	 *   3) We have all data this block holds and eod. Pull it out and indicate
	 *      EOD.
	 *   4) We have all data this block holds and !eod. Pull it out and pull
	 *      the next header.
	 */
 

	/* If we have more data than this block holds... */
	if (dc->buflen > dc->count)
	{
		/* This would be illegal. */
		assert(dc->eod == 0);

		/* Copy it out. */
		*buf = malloc(dc->count);
		*len = dc->count;
		*off = dc->off;
		memcpy(*buf, dc->buf, dc->count);
		memmove(dc->buf, dc->buf + dc->count, dc->buflen - dc->count);
		dc->buflen -= dc->count;
		dc->off    += dc->count;
		dc->count   = 0;
	} else
	{
		/* Give them the actual buffer. */
		*len       = dc->buflen;
		*buf       = dc->buf;
		*off       = dc->off;
		dc->buf    = NULL;
		dc->count -= dc->buflen;
		dc->off   += dc->buflen;
		dc->buflen = 0;
	}

	*off += dch->partial_off;

	if (dc->eod == 1 && dc->count == 0)
	{
		/* Mark the channel as EOD. */
		dc->state = DC_STATE_EOD;
		/* Increment the eod count. */
		ebpd->eods++;

		/* If we have received the EOF header w/ the expected eod count...*/
		if (ebpd->eeods > 0)
		{
			/* If all channels are now at EOD... */
			if (ebpd->eods == ebpd->eeods)
			{
				/* Indicate EOF to the caller. */
				*eof = 1;
			}
		}

		return EC_SUCCESS;
	}

	/* If the block is done. */
	if (dc->count == 0)
	{
		/* Read the next header. */
		dc->state = DC_STATE_HEADER_PULLUP;

		return EC_SUCCESS;
	}

	/* There is still some block left to read, stay in READ READY. */
	return EC_SUCCESS;
}

static errcode_t
_f_eb_write_ready(dch_t * dch, int * ready)
{
	errcode_t ec   = EC_SUCCESS;
	ebpd_t  * ebpd = (ebpd_t *) dch->privdata;
	int       i    = 0;

	*ready = 0;
	ec = _f_eb_poll(dch);
	if (ec)
		return ec;

	for (i = 0; i < ebpd->dccnt; i++)
	{
		if (ebpd->dcs[i].state == DC_STATE_READY)
		{
			*ready = 1;
			return ec;
		}
	}
	return ec;
}

static errcode_t
_f_eb_write(dch_t        * dch,
           char          * buf,
           globus_off_t    off,
           size_t          len,
           int             eof)
{
	errcode_t ec   = EC_SUCCESS;
	dc_t    * dc   = NULL;
	ebpd_t  * ebpd = (ebpd_t *) dch->privdata;
	int       i    = 0;

	off -= dch->partial_off;

	if (buf && len)
	{
		while (!dc)
		{
			ec = _f_eb_poll(dch);
			if (ec)
				return ec;

			for (i = 0; !dc && i < ebpd->dccnt; i++)
			{
				if (ebpd->dcs[i].state == DC_STATE_READY)
					dc = &ebpd->dcs[i];
			}
		}

		dc->buf    = buf;
		dc->off    = off;
		dc->count  = len;
		dc->buflen = len;
		dc->state  = DC_STATE_PUSH_HEADER;
	}

	/* Set state to WRITE_PUSH */
	if (!eof)
		return ec;

	/* Flush data. Send EOF once to each server. */
	for (i = 0; i < ebpd->dccnt; i += s_parallel())
	{
		while (ebpd->dcs[i].state != DC_STATE_READY)
		{
			ec = _f_eb_poll(dch);
			if (ec)
				return ec;
		}
		ebpd->dcs[i].state = DC_STATE_PUSH_EOF;
	}

	/* Flush EOF. Send EOD. */
	for (i = 0; i < ebpd->dccnt; i++)
	{
		while (ebpd->dcs[i].state != DC_STATE_READY)
		{
			ec = _f_eb_poll(dch);
			if (ec)
				return ec;
		}
		ebpd->dcs[i].state = DC_STATE_PUSH_EOD;
	}

	/* Flush EOD. */
	for (i = 0; i < ebpd->dccnt; i++)
	{
		while (ebpd->dcs[i].state != DC_STATE_EOD)
		{
			ec = _f_eb_poll(dch);
			if (ec)
				return ec;
		}
	}

	return ec;
}

static void
_f_eb_close(dch_t * dch)
{
	errcode_t ec   = EC_SUCCESS;
	ebpd_t  * ebpd = NULL;
	int i  = 0;
	int wr = 0;
	int rd = 0;

	if (!dch->privdata)
		return;

	ebpd = (ebpd_t *) dch->privdata;

	for (i = 0; i < ebpd->dccnt; i++)
	{
		if (ebpd->dcs[i].state == DC_STATE_CONNECT)
		{
			do {
				rd = wr = 1;
				ec = net_poll(ebpd->dcs[i].nh, &rd, &wr, -1);
			} while (ec == EC_SUCCESS && !wr && !rd);
			ec_destroy(ec);
		}
		FREE(ebpd->dcs[i].buf);
		net_destroy(ebpd->dcs[i].nh);
		gsi_destroy(ebpd->dcs[i].gh);
	}

	FREE(ebpd->dcs);
	FREE(dch->privdata);
	dch->privdata = NULL;
}


const dci_t Ftp_eb_dci = {
	_f_eb_active,
	_f_eb_passive,
	_f_eb_read_ready,
	_f_eb_read,
	_f_eb_write_ready,
	_f_eb_write,
	_f_eb_close,
};

static errcode_t
_f_eb_read_pullup(dc_t * dc)
{
	errcode_t     ec   = EC_SUCCESS;
	int           rdy  = 0;
	char *        buf  = NULL;
	size_t        len  = 0;
	int           eof  = 0;

	if (dc->eof)
		return ec;

	ec = gsi_dc_fl_read(dc->gh, dc->nh);
	if (ec)
		return ec;

	rdy = gsi_dc_ready(dc->gh, dc->nh, 1);
	if (!rdy)
		return ec;

	ec = gsi_dc_read(dc->gh, dc->nh, &buf, &len, &eof);
	if (ec)
		return ec;

	if (eof)
		dc->eof = eof;

	dc->buf = (char *) realloc(dc->buf, dc->buflen + len);
	memcpy(dc->buf + dc->buflen, buf, len);
	dc->buflen += len;
	Free(buf);

	return ec;
}

static errcode_t
_f_eb_header_pullup(ebpd_t * ebpd, dc_t * dc)
{
	errcode_t     ec   = EC_SUCCESS;
	int           desc = 0;

	ec = _f_eb_read_pullup(dc);
	if (ec)
		return ec;

	if (dc->buflen < EB_HEADER_LEN)
		return ec;

	/* Initialize the count and offset. */
	dc->count = 0;
	dc->off   = 0;

	/* Find the descriptor field. */
	desc = (int) dc->buf[0];

	/* Eod */
	if (desc & 0x08)
		dc->eod = 1;

	/*
	 * EOF Header. This header is a different format than the others:
	 *   8 bit descriptor, 64 bits unused, 64 bit expected EOD count
	 * No data follows this header.
	 */
	if (desc & 0x40)
	{
		ebpd->eeods = (((int)dc->buf[13]) << 24) +
		              (((int)dc->buf[14]) << 16) +
		              (((int)dc->buf[15]) <<  8) +
		              (((int)dc->buf[16]));

		memmove(dc->buf, dc->buf + EB_HEADER_LEN, dc->buflen - EB_HEADER_LEN);
		dc->buflen -= EB_HEADER_LEN;

		/*
		 * If we received EOD, we are done (since we can not receive more data
		 * on this header. Go to READ READY with count = 0 so that we can set
		 * eof when the caller calls read.
		 */
		if (dc->eod)
			dc->state = DC_STATE_READ_READY;

		/* If not eod, stay in header pullup. */

		return ec;
	}

	dc->count = (((globus_off_t)dc->buf[1] & 0xFF) << 56) +
	            (((globus_off_t)dc->buf[2] & 0xFF) << 48) +
	            (((globus_off_t)dc->buf[3] & 0xFF) << 40) +
	            (((globus_off_t)dc->buf[4] & 0xFF) << 32) +
	            (((globus_off_t)dc->buf[5] & 0xFF) << 24) +
	            (((globus_off_t)dc->buf[6] & 0xFF) << 16) +
	            (((globus_off_t)dc->buf[7] & 0xFF) <<  8) +
	            (((globus_off_t)dc->buf[8] & 0xFF));

	dc->off = (((globus_off_t)dc->buf[9]  & 0xFF) << 56) +
	          (((globus_off_t)dc->buf[10] & 0xFF) << 48) +
	          (((globus_off_t)dc->buf[11] & 0xFF) << 40) +
	          (((globus_off_t)dc->buf[12] & 0xFF) << 32) +
	          (((globus_off_t)dc->buf[13] & 0xFF) << 24) +
	          (((globus_off_t)dc->buf[14] & 0xFF) << 16) +
	          (((globus_off_t)dc->buf[15] & 0xFF) <<  8) +
	          (((globus_off_t)dc->buf[16] & 0xFF));

	memmove(dc->buf, dc->buf + EB_HEADER_LEN, dc->buflen - EB_HEADER_LEN);
	dc->buflen -= EB_HEADER_LEN;

	/* Mark us as read ready. */
	dc->state = DC_STATE_READ_READY;

	return ec;
}

static errcode_t
_f_eb_poll(dch_t * dch)
{
	errcode_t ec   = EC_SUCCESS;
	int       done = 0;
	int       i    = 0;
	ebpd_t  * ebpd = (ebpd_t *) dch->privdata;
	dc_t    * dc   = NULL;
	nh_t    * nh   = NULL;

	for (i = 0; !ec && i < ebpd->dccnt; i++)
	{
		dc = &ebpd->dcs[i];

		switch (dc->state)
		{
		case DC_STATE_CONNECT:
			dc->state = DC_STATE_CONNECT_AUTH;
		case DC_STATE_CONNECT_AUTH:
			ec = gsi_dc_auth(&dc->gh, dc->nh, dch->pbsz, dch->dcau, 0, &done);

			if (!ec && done)
				dc->state = DC_STATE_READY;
			break;

		case DC_STATE_ACCEPT:
			ec = net_accept(dc->nh, &nh);
			if (ec || !nh)
				break;

			ebpd->dccnt++;
			ebpd->dcs = (dc_t*) realloc(ebpd->dcs, sizeof(dc_t)*ebpd->dccnt);
			memset(&ebpd->dcs[ebpd->dccnt-1], 0, sizeof(dc_t));
			ebpd->dcs[ebpd->dccnt-1].nh = nh;
			ebpd->dcs[ebpd->dccnt-1].state = DC_STATE_ACCEPT_AUTH;
			break;

		case DC_STATE_ACCEPT_AUTH:
			ec = gsi_dc_auth(&dc->gh, dc->nh, dch->pbsz, dch->dcau, 1, &done);

			if (!ec && done)
				dc->state = DC_STATE_HEADER_PULLUP;
			break;

		case DC_STATE_HEADER_PULLUP:
			ec = _f_eb_header_pullup(ebpd, dc);
			break;

		case DC_STATE_READ_READY:
			/*
			 * It may be the case that we have already read our entire block or
			 * perhaps we aren't receiving a block (count = 0). So only bother
			 * with the read if we know we have data to read.
			 */
			if (dc->count > dc->buflen)
				ec = _f_eb_read_pullup(dc);
			break;

		case DC_STATE_PUSH_HEADER:
			ec = _f_eb_push_header(ebpd, dc);
			break;

		case DC_STATE_PUSH_DATA:
			ec = _f_eb_push_data(ebpd, dc);
			break;

		case DC_STATE_PUSH_EOD:
			ec = _f_eb_push_eod(ebpd, dc);
			break;

		case DC_STATE_PUSH_EOF:
			ec = _f_eb_push_eof(ebpd, dc);
			break;

		case DC_STATE_FLUSH_HEADER:
		case DC_STATE_FLUSH_DATA:
		case DC_STATE_FLUSH_EOF:
		case DC_STATE_FLUSH_EOD:
			ec = gsi_dc_fl_write(dc->gh, dc->nh);

			if (gsi_dc_ready(dc->gh, dc->nh, 0))
			{
				switch (dc->state)
				{
				case DC_STATE_FLUSH_HEADER:
					dc->state = DC_STATE_PUSH_DATA;
					break;
				case DC_STATE_FLUSH_DATA:
				case DC_STATE_FLUSH_EOF:
					dc->state = DC_STATE_READY;
					break;
				case DC_STATE_FLUSH_EOD:
					dc->state = DC_STATE_EOD;
					ebpd->eods++;
					break;
				}
			}
			break;
		}
	}
	return ec;
}

static char *
_f_eb_header(char desc, globus_off_t count, globus_off_t off)
{
	char * cptr = (char *) malloc(EB_HEADER_LEN);

	cptr[0] = desc;

	cptr[1] = (count >> 56) & 0xFF;
	cptr[2] = (count >> 48) & 0xFF;
	cptr[3] = (count >> 40) & 0xFF;
	cptr[4] = (count >> 32) & 0xFF;
	cptr[5] = (count >> 24) & 0xFF;
	cptr[6] = (count >> 16) & 0xFF;
	cptr[7] = (count >>  8) & 0xFF;
	cptr[8] = (count >>  0) & 0xFF;

	cptr[9]  = (off >> 56) & 0xFF;
	cptr[10] = (off >> 48) & 0xFF;
	cptr[11] = (off >> 40) & 0xFF;
	cptr[12] = (off >> 32) & 0xFF;
	cptr[13] = (off >> 24) & 0xFF;
	cptr[14] = (off >> 16) & 0xFF;
	cptr[15] = (off >>  8) & 0xFF;
	cptr[16] = (off >>  0) & 0xFF;

	return cptr;
}

static errcode_t
_f_eb_push_header(ebpd_t * ebpd, dc_t * dc)
{
	errcode_t  ec = EC_SUCCESS;
	char * header = _f_eb_header(0, dc->count, dc->off);

	ec = gsi_dc_write(dc->gh, dc->nh, header, EB_HEADER_LEN, 0);
	dc->state = DC_STATE_FLUSH_HEADER;
	return ec;
}

static errcode_t
_f_eb_push_eod(ebpd_t * ebpd, dc_t * dc)
{
        errcode_t  ec = EC_SUCCESS;
	/* 0x08 = EOD */
	/* 0x04 = Close data channel . */
	char * header = _f_eb_header(0x08|0x04, 0, 0);

	ec = gsi_dc_write(dc->gh, dc->nh, header, EB_HEADER_LEN, 0);
	dc->state = DC_STATE_FLUSH_EOD;
	return ec;
}

static errcode_t
_f_eb_push_eof(ebpd_t * ebpd, dc_t * dc)
{
	errcode_t  ec = EC_SUCCESS;
	char * header = _f_eb_header(0x40, 0, s_parallel());

	ec = gsi_dc_write(dc->gh, dc->nh, header, EB_HEADER_LEN, 0);
	dc->state = DC_STATE_FLUSH_EOF;
	return ec;
}

static errcode_t
_f_eb_push_data(ebpd_t * ebpd, dc_t * dc)
{
	errcode_t ec = EC_SUCCESS;

	ec = gsi_dc_write(dc->gh, dc->nh, dc->buf, dc->buflen, 0);
	dc->buf    = NULL;
	dc->buflen = 0;
	dc->count  = 0;
	dc->off    = 0;
	dc->state  = DC_STATE_FLUSH_DATA;

	return ec;
}

