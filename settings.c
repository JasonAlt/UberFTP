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
#include <stdio.h>

#include "settings.h"
#include "output.h"
#include "config.h"
#include "misc.h"

#ifdef DMALLOC
#include "dmalloc.h"
#endif /* DMALLOC */


/* These are the defaults. */
static int binary    = 1;
static int cksum     = 0;
static int dcau      = 1; /* 0 none, 1 self, 2 subject */
static int debug     = DEBUG_ERRS_ONLY;
static int debug_set = 0;
static int hash      = 0;
static int globon    = 1;
static int keepalive = 0;
static unsigned short min_port  = 0; /* TCP_PORT_RANGE min */
static unsigned short max_port  = 0; /* TCP_PORT_RANGE max */
static unsigned short min_src   = 0; /* TCP_SOURCE_RANGE min */
static unsigned short max_src   = 0; /* TCP_SOURCE_RANGE max */
static int order     = ORDER_BY_NONE;
static int parallel  = 1;
static int prot      = 0; /* 0 clear, 1 safe, 2 confidential, 3 private */
static int retry     = 0;
static int runique   = 0;
static int stream    = 1;
static int sunique   = 0;
static int waiton    = 0;
static long long pbsz      = 0; /* Default, determined on the fly */
static long long tcpbuf    = DEFAULT_TCP_BUFFER_SIZE;
static long long blocksize = DEFAULT_BLKSIZE;
static char * dcau_subject = NULL;
static char * cos          = NULL;
static char * family       = NULL;
static char * resume       = NULL;

#ifdef MSSFTP
static int passive   = 0;
#else /* MSSFTP */
static int passive   = 1;
#endif /* MSSFTP */

void
s_init()
{
	int            rv   = 0;
	char         * cval = NULL;
	unsigned short min  = 0;
	unsigned short max  = 0;

	if (getenv("UBERFTP_ACTIVE_MODE"))
		passive = 0;

	/* GLOBUS_TCP_PORT_RANGE for backwards compatibility */
	cval = getenv("GLOBUS_TCP_PORT_RANGE");
	if (cval)
	{
		rv = sscanf(cval, "%hu,%hu", &min, &max);
		if (rv != 2)
			rv = sscanf(cval, "%hu %hu", &min, &max);
		if (rv == 2 && min <= max)
		{
			min_port = min;
			max_port = max;
		}
	}

	/* UBERFTP_TCP_PORT_RANGE for forwards compatibility */
	/* Overrides GLOBUS_* */
	cval = getenv("UBERFTP_TCP_PORT_RANGE");
	if (cval)
	{
		rv = sscanf(cval, "%hu,%hu", &min, &max);
		if (rv != 2)
			rv = sscanf(cval, "%hu %hu", &min, &max);
		if (rv == 2 && min <= max)
		{
			min_port = min;
			max_port = max;
		}
	}

	/* GLOBUS_TCP_SOURCE_RANGE for backwards compatibility */
	cval = getenv("GLOBUS_TCP_SOURCE_RANGE");
	if (cval)
	{
		rv = sscanf(cval, "%hu,%hu", &min, &max);
		if (rv != 2)
			rv = sscanf(cval, "%hu %hu", &min, &max);
		if (rv == 2 && min <= max)
		{
			min_src = min;
			max_src = max;
		}
	}

	/* UBERFTP_TCP_SOURCE_RANGE for forwards compatibility */
	/* Overrides GLOBUS_* */
	cval = getenv("UBERFTP_TCP_SOURCE_RANGE");
	if (cval)
	{
		rv = sscanf(cval, "%hu,%hu", &min, &max);
		if (rv != 2)
			rv = sscanf(cval, "%hu %hu", &min, &max);
		if (rv == 2 && min <= max)
		{
			min_src = min;
			max_src = max;
		}
	}
}

void 
s_setactive()
{
	passive = 0;
}

void 
s_setascii()
{
	binary = 0;
}

void 
s_setbinary()
{
	binary = 1;
}

void
s_setblocksize(long long size)
{
	blocksize = size;
	if (size == 0)
		blocksize = DEFAULT_BLKSIZE;
}

void
s_setcksum(int on)
{
	cksum = on ? 1 : 0;
}

void
s_setcos(char * Cos)
{
	FREE(cos);

	cos = Strdup(Cos);
}

void
s_setdcau(int lvl, char * subject)
{
	dcau = lvl;
	if (dcau_subject)
		FREE(dcau_subject);
	dcau_subject = Strdup(subject);
}

void
s_setdebug(int lvl)
{
	if (lvl < DEBUG_MIN)
		debug = DEBUG_MIN;
	else if (lvl > DEBUG_MAX)
		debug = DEBUG_MAX;
	else
		debug = lvl;
	debug_set = 1;
}

void
s_setglob(int on)
{
	globon = on;
}

void
s_setfamily(char * fam)
{
	FREE(family);
	family = Strdup(fam);
}

void
s_seteb()
{
	stream = 0;
}

void
s_sethash()
{
	hash = !hash;
}

void
s_setkeepalive(int seconds)
{
	keepalive = seconds;
}

void
s_setorder(int o)
{
	order = o;
}

void
s_setparallel(int cnt)
{
	parallel = cnt;
	if (parallel < 1)
		parallel = 1;
}

void
s_setpbsz(long long length)
{
	pbsz = length;
	if (pbsz == 0)
		pbsz = 0;
}

void 
s_setpassive()
{
	passive = 1;
}

void
s_setprot(int lvl)
{
	prot = lvl;
}

void
s_setretry(int cnt)
{
	retry = cnt;
	if (retry < 0)
		retry = 0;
}

void
s_setresume(char * path)
{
	FREE(resume);
	resume = Strdup(path);
}

void
s_setrunique()
{
	runique = !runique;
}

void
s_setstream()
{
	stream = 1;
}

void
s_setsunique()
{
	sunique = !sunique;
}

void
s_settcpbuf(long long size)
{
	tcpbuf = size;
	if (size == 0)
		tcpbuf = 0;
}

void
s_setwait()
{
	waiton = !waiton;
}

int
s_ascii()
{
	return !binary;
}

long long
s_blocksize()
{
	return blocksize;
}

int
s_cksum()
{
	return cksum;
}

char *
s_cos()
{
	return cos;
}

int
s_dcau()
{
	return dcau;
}

char *
s_dcau_subject()
{
	return Strdup(dcau_subject);
}

int
s_debug()
{
	return debug;
}

int
s_debug_set()
{
	return debug_set;
}

int
s_glob()
{
	return globon;
}

char *
s_family()
{
	return family;
}

int
s_hash()
{
	return hash;
}

int
s_keepalive()
{
	return keepalive;
}

unsigned short
s_maxsrc(void)
{
	return max_src;
}

unsigned short
s_maxport(void)
{
	return max_port;
}

unsigned short
s_minsrc(void)
{
	return min_src;
}

unsigned short
s_minport(void)
{
	return min_port;
}

int
s_order()
{
	return order;
}

int
s_parallel()
{
	return parallel;
}

long long
s_pbsz()
{
	return pbsz;
}

int
s_passive()
{
	return passive;
}

int
s_prot()
{
	return prot;
}

int
s_retry()
{
	return retry;
}

char *
s_resume()
{
	return resume;
}

int
s_runique()
{
	return runique;
}

int
s_stream()
{
	return stream;
}

int
s_sunique()
{
	return sunique;
}

long long
s_tcpbuf()
{
	return tcpbuf;
}

int
s_wait()
{
	return waiton;
}
