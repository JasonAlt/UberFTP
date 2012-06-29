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
#include <gssapi.h>
#include <stdarg.h>
#include <stdio.h>

#include "settings.h"
#include "errcode.h"
#include "misc.h"

#ifdef DMALLOC
#include "dmalloc.h"
#endif /* DMALLOC */


struct errcode_s {
	int                              refs;
	int                              code;
	int                              flags;
	char                          ** errmsg;
	OM_uint32                        major;
	OM_uint32                        minor;
};


errcode_t
ec_create(OM_uint32 major,
          OM_uint32 minor,
          char    * fmt,
          ...)
{
	int         len = 0;
	int         ret = 0;
	va_list     ap;
	errcode_t errcode   = (errcode_t) malloc(sizeof(struct errcode_s));

	errcode->refs         = 1;
	errcode->errmsg       = NULL;
	errcode->major        = major;
	errcode->minor        = minor;
	errcode->flags        = 0;

	/* Now the user friendly message. */
	if (fmt != NULL)
	{
		/*
		 * Initialize since Tru64 has a problem with it being NULL.
		 */
		len = 128;
		errcode->errmsg = (char **) malloc(2*sizeof(char *));
		errcode->errmsg[0] = malloc(len);
		errcode->errmsg[1] = NULL;

		while(1)
		{
			/* This will calculate the length. */
			va_start (ap, fmt);
			ret = vsnprintf(errcode->errmsg[0], len, fmt, ap);
			va_end(ap);

			if (ret == -1)
				len += 128;
			else if (ret < len)
				break;
			else
				len = ret + 1;

			errcode->errmsg[0] = (char *) realloc(errcode->errmsg[0], len);
		}
	}

	return errcode;
}

void
ec_prepend_msg(errcode_t errcode, char * fmt, ...)
{
	int         len   = 0;
	int         ret   = 0;
	int         index = 0;
	va_list     ap;

	for (index = 0; errcode->errmsg[index]; index++);
	errcode->errmsg = (char **)realloc(errcode->errmsg,(index+2)*sizeof(char*));
	errcode->errmsg[index + 1] = NULL;

	if (fmt != NULL)
	{
		/*
		 * Initialize since Tru64 has a problem with it being NULL.
		 */
		len = 128;
		errcode->errmsg[index] = malloc(len);

		while(1)
		{
			/* This will calculate the length. */
			va_start (ap, fmt);
			ret = vsnprintf(errcode->errmsg[index], len, fmt, ap);
			va_end(ap);

			if (ret == -1)
				len += 128;
			else if (ret < len)
				break;
			else
				len = ret + 1;

			errcode->errmsg[index] = (char *) realloc(errcode->errmsg[index], len);
		}
	}
}

void
ec_set_flag(errcode_t errcode, int flag)
{
	errcode->flags |= flag;
}

int
ec_retry(errcode_t errcode)
{
	if (!errcode)
		return 0;
	if (errcode->flags & EC_FLAG_SHOULD_RETRY)
		return 1;
	if ((errcode->flags & EC_FLAG_CAN_RETRY) && s_retry())
		return 1;
	return 0;
}

void
ec_destroy(errcode_t errcode)
{
	int c = 0;

	if (errcode == EC_SUCCESS)
		return;

	if (errcode->refs-- > 1)
		return;

	for (c = 0; errcode->errmsg != NULL && errcode->errmsg[c] != NULL; c++)
		FREE(errcode->errmsg[c]);

	if (errcode->errmsg != NULL)
		FREE(errcode->errmsg);

	FREE(errcode);

	return;
}


void
ec_print(errcode_t errcode)
{
	int    c      = 0;
	OM_uint32  mcntxt = 0;
	OM_uint32  maj = GSS_S_COMPLETE;
	OM_uint32  min = GSS_S_COMPLETE;
	gss_buffer_desc msg;

	if (errcode == EC_SUCCESS)
		return;

	/* Print in reverse order */
	for (c = 0; errcode->errmsg != NULL && errcode->errmsg[c] != NULL; c++);

	for (c--; errcode->errmsg != NULL && c >= 0; c--)
	{
		fprintf(stderr, "%s", errcode->errmsg[c]);
		if (*(errcode->errmsg[c] + strlen(errcode->errmsg[c]) - 1) != '\n')
			fprintf(stderr, "\n");
	}

	if (errcode->major != GSS_S_COMPLETE)
	{
		for (mcntxt = 0; !mcntxt;)
		{
			maj = gss_display_status(&min,
			                          errcode->major,
			                          GSS_C_GSS_CODE,
			                          GSS_C_NULL_OID,
			                         &mcntxt,
			                         &msg);

			if (maj == GSS_S_COMPLETE || maj == GSS_S_CONTINUE_NEEDED)
			{
				if (msg.value)
					fprintf(stderr, "%s\n", (char *)msg.value);
			}
			gss_release_buffer(&min, &msg);

			if (maj != GSS_S_CONTINUE_NEEDED)
				break;
		}

		for (mcntxt = 0; !mcntxt;)
		{
			maj = gss_display_status(&min,
			                          errcode->minor,
			                          GSS_C_MECH_CODE,
			                          GSS_C_NULL_OID,
			                         &mcntxt,
			                         &msg);

			if (maj == GSS_S_COMPLETE || maj == GSS_S_CONTINUE_NEEDED)
			{
				if (msg.value)
					fprintf(stderr, "%s\n", (char *)msg.value);
			}
			gss_release_buffer(&min, &msg);

			if (maj != GSS_S_CONTINUE_NEEDED)
				break;
		}
	}

	fflush(stderr);
}

errcode_t
ec_dup(errcode_t ec)
{
	if (ec != EC_SUCCESS)
		ec->refs++;

	return ec;
}
