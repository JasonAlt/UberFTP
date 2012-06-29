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
 * http://dims.ncsa.uiuc.edu/set
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
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#include "errcode.h"
#include "radix.h"

#ifdef DMALLOC
#include "dmalloc.h"
#endif /* DMALLOC */


static char *radixN =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static char pad = '=';

errcode_t
radix_encode(char * inbuf, char ** outbuf, int * len)
{
	int i = 0;
	int j = 0;
	unsigned char c;

	*outbuf = (char *) malloc(*len + (*len)/3 + 4);

	for (i=0, j=0; i < *len; i++)
	{
	    switch (i%3)
		{
		case 0:
		    (*outbuf)[j++] = radixN[((unsigned char)inbuf[i])>>2];
		    c = (inbuf[i]&3)<<4;
		    break;
		case 1:
		    (*outbuf)[j++] = radixN[c|((unsigned char) inbuf[i])>>4];
		    c = (inbuf[i]&15)<<2;
		    break;
		case 2:
		    (*outbuf)[j++] = radixN[c|((unsigned char) inbuf[i])>>6];
		    (*outbuf)[j++] = radixN[((unsigned char) inbuf[i])&63];
		    c = 0;
	    }
	}

	if (i%3)
		(*outbuf)[j++] = radixN[c];

	switch (i%3)
	{
	case 1:
		(*outbuf)[j++] = pad;
	case 2:
		(*outbuf)[j++] = pad;
	}

	(*outbuf)[j] = '\0';
	*len      = j;

	return EC_SUCCESS;
}

errcode_t
radix_decode(char * inbuf, char ** outbuf, int * len)
{
	int i = 0;
	int j = 0;
	int D = 0;
	char *p = NULL;

	*outbuf = (char *) malloc(2 * (*len));
	for (i=0,j=0; i < *len &&  inbuf[i] != pad; i++)
	{
		if ((p = strchr(radixN, inbuf[i])) == NULL)
			return ec_create(EC_GSI_SUCCESS,
			                 EC_GSI_SUCCESS,
			                 "Error trying to radix decode (%d\\%d). %s",
			                 i,
			                 inbuf[i],
			                 "Bad character in encoding.");

		D = p - radixN;
	    switch (i&3)
		{
		case 0:
			(*outbuf)[j] = D<<2;
			break;
		case 1:
			(*outbuf)[j++] |= D>>4;
			(*outbuf)[j] = (D&15)<<4;
			break;
		case 2:
			(*outbuf)[j++] |= D>>2;
			(*outbuf)[j] = (D&3)<<6;
			break;
		case 3:
			(*outbuf)[j++] |= D;
		}
	}

	switch (i&3)
	{
	case 1:
		return ec_create(EC_GSI_SUCCESS,
		                 EC_GSI_SUCCESS,
		                 "radix decode error: %s",
		                 "Decoded # of bits not a multiple of 8");

	case 2:
		if (D&15)
			return ec_create(EC_GSI_SUCCESS,
			                 EC_GSI_SUCCESS,
			                 "radix decode error: %s",
			                 "Decoded # of bits not a multiple of 8");

		if (strncmp((char *)&inbuf[i], "==", 2))
			return ec_create(EC_GSI_SUCCESS,
			                 EC_GSI_SUCCESS,
			                 "radix decode error: %s",
			                 "Encoding not properly padded");

		break;

	case 3:
		if (D&3)
			return ec_create(EC_GSI_SUCCESS,
			                 EC_GSI_SUCCESS,
			                 "radix decode error: %s",
			                 "Decoded # of bits not a multiple of 8");
		if (strncmp((char *)&inbuf[i], "=", 1))
			return ec_create(EC_GSI_SUCCESS,
			                 EC_GSI_SUCCESS,
			                 "radix decode error: %s",
			                 "Encoding not properly padded");
	}
	*len = j;

	return EC_SUCCESS;
}

