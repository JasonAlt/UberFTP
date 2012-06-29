/*
 * University of Illinois/NCSA Open Source License
 *
 * Copyright © 2003-2012 NCSA. All rights reserved.
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
#ifndef UBER_GSI_H
#define UBER_GSI_H

#include "errcode.h"
#include "ftp.h"

typedef struct _gsi_handle gh_t;

errcode_t gsi_init();

errcode_t
gsi_cc_init(gh_t ** gh, char * host);

void
gsi_destroy(gh_t * gh);

errcode_t
gsi_init_sec_context(gh_t * gh, char * input_token, char ** output_token);

errcode_t
gsi_cc_wrap(gh_t * gh, char * inmsg, char ** wrapped_msg);

errcode_t
gsi_cc_unwrap(gh_t * gh, char * inmsg, char ** wrapped_msg, int len);

/* Data Channel Operations. */
errcode_t
gsi_dc_auth(gh_t ** ghp, nh_t * nh, int pbsz, int dcau, int accept, int * done);

errcode_t
gsi_dc_fl_read(gh_t * gh, nh_t * nh);

errcode_t
gsi_dc_fl_write(gh_t * gh, nh_t * nh);

errcode_t
gsi_dc_read(gh_t  * gh, 
            nh_t  * nh, 
            char  ** buf, 
            size_t * len, 
            int   * eof);

errcode_t
gsi_dc_write(gh_t  * gh,
             nh_t  * nh,
             char  * buf,
             size_t  len,
             int     eof);

int
gsi_dc_ready(gh_t * gh, nh_t * nh, int read);

errcode_t
gsi_pbsz_maxpmsg(gh_t * gh, int umsglen, int * pmsglen);

errcode_t
gsi_pbsz_maxumsg(gh_t * gh, int pmsglen, int * umsglen);

#endif /* UBER_GSI_H */
