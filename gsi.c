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

/* System includes. */
#include <sys/socket.h>
#include <sys/types.h>
#include <assert.h>
#include <stdlib.h>
#include <gssapi.h>
#include <netdb.h>

/* Local includes. */
#include "errcode.h"
#include "settings.h"
#include "radix.h"
#include "misc.h"
#include "gsi.h"

#ifdef DMALLOC
#include "dmalloc.h"
#endif /* DMALLOC */


#define SSL_TOK_LEN(x) (((((int)(x)[3]) << 8) & 0xFF00) | \
                        ((((int)(x)[4]) << 0) & 0xFF) )
#define GSI_WRAP_LEN(x) (((((int)(x)[0]) << 24) & 0xFF000000) | \
                         ((((int)(x)[1]) << 16) & 0x00FF0000) | \
                         ((((int)(x)[2]) <<  8) & 0x0000FF00) | \
                         ((((int)(x)[3]) <<  0) & 0x000000FF))

#define G_S_READ  0x00
#define G_S_GEN   0x01
#define G_S_WRITE 0x02
struct _gsi_handle {
	gss_cred_id_t creds;
	gss_ctx_id_t  cntxt;
	gss_name_t    target;
	int           success;

	/* Data channel operations. */
	int    upbsz; /* Unwrapped message buffer length. */
	int    state;
	int    done;
	size_t    cnt;
	int    len;
	int    eof;
	char * buf;
	char * ubuf; /* For writing, the unwrapped buffer. */
	int    ulen; /* For writing, the unwrapped length. */

	int    dcau; /* 0 no, 1 yes. */ /* Use s_dcau() for settings. */
	int    pbsz; /* Protection buffer size */
};

errcode_t
_g_compare_names(gh_t * gh);

errcode_t
_g_gen_token(gh_t * gh, int init);

errcode_t
_g_send_token(gh_t * gh, nh_t * nh);

errcode_t
_g_recv_token(gh_t * gh, nh_t * nh);

errcode_t
_g_acquire_cred(gss_cred_id_t * credp);

errcode_t
gsi_init()
{
	OM_uint32     minor;
	errcode_t     ec   = EC_SUCCESS;
	gss_cred_id_t cred = GSS_C_NO_CREDENTIAL;

	ec = _g_acquire_cred(&cred);

	if (cred != GSS_C_NO_CREDENTIAL)
		gss_release_cred(&minor, &cred);

	return ec;
}

errcode_t
gsi_cc_init(gh_t ** ghp, char * host)
{
	errcode_t       ec = EC_SUCCESS;
	OM_uint32       major;
	OM_uint32       minor;
	gss_buffer_desc input_name_buffer; /* Non pointer version of gss_buffer_t */
	char          * sname  = NULL;
	gh_t          * gh     = NULL;

	gh = *ghp = (gh_t *) malloc(sizeof(gh_t));
	memset(gh, 0, sizeof(gh_t));
	gh->creds  = GSS_C_NO_CREDENTIAL;
	gh->cntxt  = GSS_C_NO_CONTEXT;
	gh->target = GSS_C_NO_NAME;

	/*
	 * Acquire the user's credentials.
	 */
	ec = _g_acquire_cred(&gh->creds);
	if (ec)
		goto cleanup;

	/*
	 * Construct the printable service name. 
	 */
	sname = Sprintf(NULL, "host@%s", host);

	/*
	 * Convert the name to the GSS internal format.
	 */
	input_name_buffer.value  = sname;
	input_name_buffer.length = strlen(sname) + 1;
	major = gss_import_name(&minor,
	                        &input_name_buffer,
	                         GSS_C_NT_HOSTBASED_SERVICE,
	                        &gh->target);

	if (major != GSS_S_COMPLETE)
	{
		ec = ec_create(major,
		               minor,
		               "Failed to import service name");
		goto cleanup;
	}

cleanup:
	if (ec != EC_SUCCESS)
	{
		gsi_destroy(gh);
		*ghp = NULL;
	}

	FREE(sname);

	return ec;
}

void
gsi_destroy(gh_t * gh)
{
	OM_uint32 minor;

	if (!gh)
		return;

	if (gh->cntxt != GSS_C_NO_CONTEXT)
		gss_delete_sec_context(&minor, &gh->cntxt, GSS_C_NO_BUFFER);

	if (gh->creds != GSS_C_NO_CREDENTIAL)
		gss_release_cred(&minor, &gh->creds);

	if (gh->target != GSS_C_NO_NAME)
		gss_release_name(&minor, &gh->target);

	FREE(gh->buf);
	FREE(gh);
}

errcode_t
gsi_init_sec_context(gh_t * gh, char * input_token, char ** output_token)
{
	errcode_t ec = EC_SUCCESS;
	OM_uint32 major;
	OM_uint32 minor;

	gss_buffer_desc intok;
	gss_buffer_desc outtok;
	char          * decoded_input = NULL;
	int             len           = 0;

	*output_token = NULL;

	/* Radix decode the input_token. */
	if (input_token)
	{
		len = strlen(input_token) - 2; /* Omit \r\n */
		ec = radix_decode(input_token, &decoded_input, &len);

		if (ec != EC_SUCCESS)
			goto cleanup;
	}

	intok.value  = decoded_input;
	intok.length = len;

	major = gss_init_sec_context(&minor,
	                              gh->creds,
	                             &gh->cntxt,
	                              gh->target,
	                              GSS_C_NO_OID,
	                              GSS_C_MUTUAL_FLAG |
	                                 GSS_C_REPLAY_FLAG |
	                                 GSS_C_SEQUENCE_FLAG |
	                                 GSS_C_GLOBUS_LIMITED_DELEG_PROXY_FLAG |
	                                 GSS_C_DELEG_FLAG |
	                                 GSS_C_CONF_FLAG,
	                              0,
	                              GSS_C_NO_CHANNEL_BINDINGS,
	                             &intok,
	                              NULL,
	                             &outtok,
	                              NULL,
	                              NULL);

	FREE(decoded_input);

	if (major != GSS_S_COMPLETE && major != GSS_S_CONTINUE_NEEDED)
	{
		ec = ec_create(major, minor, "Failed to init security context");
		goto cleanup;
	}

	if (major == GSS_S_COMPLETE)
		gh->success = 1;

	/* Radix encode the output token. */
	if (outtok.length)
	{
		len = outtok.length;
		ec = radix_encode(outtok.value, output_token, &len);

		gss_release_buffer(&minor, &outtok);
		if (ec != EC_SUCCESS)
			goto cleanup;
	}

cleanup:

	return ec;
}

errcode_t
gsi_cc_wrap(gh_t * gh, char * inmsg, char ** wmsg)
{
	int             len;
	int             conf_state;
	OM_uint32       major;
	OM_uint32       minor;
	gss_buffer_desc inbuf;
	gss_buffer_desc outbuf;
	errcode_t ec = EC_SUCCESS;

	/*
	 * Despite what the documents say, apparently you can not gss_wrap
	 * until the security context is complete.
	 */
	if (!gh || !gh->success)
	{
		*wmsg = Strdup(inmsg);
		return ec;
	}

	inbuf.value  = inmsg;
	inbuf.length = strlen(inmsg);
	major = gss_wrap (&minor,
	                  gh->cntxt,
		              s_prot() == 3,
	                  GSS_C_QOP_DEFAULT,
	                  &inbuf,
	                  &conf_state,
	                  &outbuf);

	if (major != GSS_S_COMPLETE)
		return ec_create(major, minor, "gss_wrap() failed");

	len = outbuf.length;
	ec  = radix_encode(outbuf.value, wmsg, &len);
	gss_release_buffer(&minor, &outbuf);
	if (ec != EC_SUCCESS)
		return ec;

	if (conf_state)
		*wmsg = Sprintf(NULL, "ENC %s", *wmsg);
	else
		*wmsg = Sprintf(NULL, "MIC %s", *wmsg);

	return ec;
}

errcode_t
gsi_cc_unwrap(gh_t * gh, char * inmsg, char ** umsg, int len)
{
	char          * decoded_msg;
	OM_uint32       major;
	OM_uint32       minor;
	gss_buffer_desc inbuf;
	gss_buffer_desc outbuf;
	errcode_t ec = EC_SUCCESS;

	if (!gh || !gh->success)
	{
		*umsg = Strdup(inmsg);
		return ec;
	}

	/* Radix decode. */
	ec  = radix_decode(inmsg, &decoded_msg, &len);

	if (ec != EC_SUCCESS)
		return ec;

	/* Gssapi unwrap. */
	inbuf.value  = decoded_msg;
	inbuf.length = len;
	major = gss_unwrap(&minor,
	                    gh->cntxt,
	                   &inbuf,
	                   &outbuf,
	                    NULL,
	                    NULL);

	FREE(decoded_msg);

	if (major != GSS_S_COMPLETE)
		return ec_create(major, minor, "Error while unwrapping message");

	*umsg = Strndup(outbuf.value, outbuf.length);
	gss_release_buffer(&minor, &outbuf);

	return ec;
}

errcode_t
gsi_dc_auth(gh_t ** ghp, nh_t * nh, int pbsz, int dcau, int accept, int * done)
{
	gh_t    * gh = *ghp;
	errcode_t ec = EC_SUCCESS;

	if (!gh)
	{
		gh = *ghp = (gh_t *) malloc(sizeof(gh_t));
		memset(gh, 0, sizeof(gh_t));
		gh->creds  = GSS_C_NO_CREDENTIAL;
		gh->cntxt  = GSS_C_NO_CONTEXT;
		gh->target = GSS_C_NO_NAME;
		gh->dcau   = dcau;
		gh->pbsz   = pbsz;

		if (accept)
			gh->state = G_S_READ;
		else
			gh->state = G_S_GEN;
	}

	*done = 1;
	if (!gh->dcau)
		return ec;

	*done = 0;
	switch (gh->state)
	{
	case G_S_READ: /* read */
		ec = _g_recv_token(gh, nh);
		break;
	case G_S_GEN: /* Gen */
		ec = _g_gen_token(gh, !accept);
		if (ec || !gh->cnt)
			break;
		/* Fall through */
	case G_S_WRITE: /* write */
		ec = _g_send_token(gh, nh);
		break;
	}

	if (gh->done && !gh->cnt)
		*done = 1;

	return ec;
}

errcode_t
gsi_dc_fl_read(gh_t * gh, nh_t * nh)
{
	errcode_t ec = EC_SUCCESS;
	size_t count = 0;

	if (gh->eof)
		return ec;

	if (gh->dcau    && 
	    s_prot()    && 
	    gh->cnt > 4 && 
	    (SSL_TOK_LEN(gh->buf) + 5) <= gh->cnt)
	{
		return ec;
	}

	if ((gh->len - gh->cnt) < s_blocksize())
	{
		gh->len += s_blocksize();
		gh->buf = (char *) realloc(gh->buf, gh->len);
	}

	count = gh->len-gh->cnt;

	ec = net_read(nh,
	              gh->buf + gh->cnt,
	             &count,
	             &gh->eof);

	if (count > 0)
		gh->cnt += count;

	return ec;
}

errcode_t
gsi_dc_fl_write(gh_t * gh, nh_t * nh)
{
    errcode_t ec = EC_SUCCESS;
    size_t count = 0;
    int off   = 0;
	gss_buffer_desc uwbuf;
	gss_buffer_desc wbuf;
	OM_uint32 major;
	OM_uint32 minor;
	int       cstate = 0;


    do {
		if (!gh->buf && gh->ubuf)
		{
			uwbuf.value  = gh->ubuf;
			uwbuf.length = gh->ulen;

			/* Can only wrap up to upbsz bytes. */
			if (gh->ulen > gh->upbsz)
				uwbuf.length = gh->upbsz;

			major = gss_wrap(&minor,
			                 gh->cntxt,
			                 s_prot() == 3,
			                 GSS_C_QOP_DEFAULT,
			                &uwbuf,
			                &cstate,
			                &wbuf);

			if (major != GSS_S_COMPLETE)
				return ec_create(major,
				                 minor,
				                 "Failed to wrap buffer.\n");

			memmove(gh->ubuf, gh->ubuf+uwbuf.length, gh->ulen-uwbuf.length);
			gh->ulen -= uwbuf.length;

			if (gh->ulen == 0)
			{
				FREE(gh->ubuf);
				gh->ubuf = 0;
			}

			gh->buf = wbuf.value;
			gh->len = wbuf.length;
			gh->cnt = wbuf.length;
		}

        if (gh->buf)
        {
            off = gh->len - gh->cnt;
            count = gh->cnt;

            ec = net_write_nb(nh,
                              gh->buf+off,
                             &count);

            if (ec == EC_SUCCESS)
            {
                gh->cnt = count;
                if (count == 0)
                {
                    gh->cnt = 0;
                    gh->len = 0;
                    FREE(gh->buf);
                    gh->buf = NULL;
                }
            }
        }
    } while ((gh->ubuf || gh->buf) && gh->eof && ec == EC_SUCCESS);

    return ec;
}


errcode_t
gsi_dc_read(gh_t   *  gh,
            nh_t   *  nh,
            char  ** buf,
            size_t *  len,
            int    *  eof)
{
	int       cstate = 0;
	gss_qop_t qstate = GSS_C_QOP_DEFAULT;
	OM_uint32 major;
	OM_uint32 minor;
	gss_buffer_desc wbuf;
	gss_buffer_desc uwbuf;

	if (gh->dcau && s_prot() && gh->cnt)
	{
		wbuf.value  = gh->buf;
		wbuf.length = SSL_TOK_LEN(gh->buf) + 5;
		major = gss_unwrap(&minor,
		                    gh->cntxt,
		                   &wbuf,
		                   &uwbuf,
		                   &cstate,
		                   &qstate);

		if (major != GSS_S_COMPLETE)
			return ec_create(major,
			                 minor,
			                 "Failed to unwrap buffer");

		*buf = (char *)uwbuf.value;
		*len = uwbuf.length;
		
		gh->cnt -= SSL_TOK_LEN(gh->buf) + 5;
		memmove(gh->buf, 
		        gh->buf + SSL_TOK_LEN(gh->buf) + 5,
		        gh->cnt);

		*eof = gh->eof;

		return EC_SUCCESS;
	}

	*buf = gh->buf;
	*len = gh->cnt;
	*eof = gh->eof;

	gh->buf = NULL;
	gh->cnt = 0;
	gh->len = 0;

	return EC_SUCCESS;
}

errcode_t
gsi_dc_write(gh_t  * gh,
             nh_t  * nh,
             char  * buf,
             size_t  len,
             int     eof)
{
    errcode_t ec = EC_SUCCESS;

   	gh->eof  = eof;

	if (gh->dcau && s_prot() && len)
	{
    	gh->ubuf = buf;
    	gh->ulen = len;
	}
	else
	{
    	gh->buf  = buf;
   		gh->cnt  = len;
   		gh->len  = len;
	}

    if (eof)
        ec = gsi_dc_fl_write(gh, nh);

    return EC_SUCCESS;
}


int
gsi_dc_ready(gh_t * gh, nh_t * nh, int read)
{
	if (read)
	{
		if (gh->eof)
			return 1;

		if (gh->cnt && (!gh->dcau || !s_prot()))
			return 1;

		if (gh->dcau && gh->cnt > 4 && (SSL_TOK_LEN(gh->buf)+5) <= gh->cnt)
			return 1;
	}

	if (!read && !gh->buf && !gh->ubuf && !gh->eof)
		return 1;

	return 0;
}

/* Finds max encoded msg size for given buffer length. */
errcode_t
gsi_pbsz_maxpmsg(gh_t * gh, int umsglen, int * pmsglen)
{
	errcode_t ec = EC_SUCCESS;
	int tryulen  = 0;
	int tryelen  = 0;
	int stryulen = 0;
	int stryelen = 0;

	tryelen = umsglen + (5 * 1024);

	while ((ec = gsi_pbsz_maxumsg(gh, tryelen, &tryulen)) == EC_SUCCESS)
	{
		if (stryulen)
		{
			if (stryulen > umsglen && tryulen < umsglen)
				break;
			if (stryulen < umsglen && tryulen > umsglen)
				break;
		}
		stryulen = tryulen;
		stryelen = tryelen;

		if (tryulen > umsglen)
			tryelen -= (5 * 1024);
		else
			tryelen += (5 * 1024);
	}
	*pmsglen = stryelen;

	return ec;
}

/* Finds max unencoded msg size */
errcode_t
gsi_pbsz_maxumsg(gh_t * gh, int pmsglen, int * umsglen)
{
	OM_uint32 size_req = pmsglen;
	OM_uint32 max_size;
	OM_uint32 major;
	OM_uint32 minor;

	major = gss_wrap_size_limit(&minor,
	                             gh->cntxt,
	                             s_prot() == 3,
	                             GSS_C_QOP_DEFAULT, 
	                             size_req,
	                            &max_size);

	if (major != GSS_S_COMPLETE)
		return ec_create(major,
		                 minor,
		                 "gss_wrap_size_limit() failed.");
	
	*umsglen = max_size;
	return EC_SUCCESS;
}

errcode_t
_g_compare_names(gh_t * gh)
{
	int       equal = 0;
	errcode_t ec = EC_SUCCESS;
	gss_name_t tname;
	gss_buffer_desc tbuf;
	OM_uint32 major;
	OM_uint32 minor;
	char * name = NULL;

	switch(s_dcau())
	{
	case 0: /* None. Will never happen. */
		assert(0);
	case 1: /* Self. */
		major = gss_inquire_cred(&minor,
		                          gh->creds,
		                         &tname,
		                          NULL,
		                          NULL,
		                          NULL);
		if (major != GSS_S_COMPLETE)
			return ec_create(major, 
			                 minor,
			                "Failed to inquire credential.");
		break;
	case 2: /* Subject. */
		tbuf.value = s_dcau_subject();
		tbuf.length = strlen(tbuf.value);
		major = gss_import_name(&minor,
		                        &tbuf,
		                         GSS_C_NO_OID,
		                        &tname);
		Free(tbuf.value);
		if (major != GSS_S_COMPLETE)
			return ec_create(major, 
			                 minor,
			                "Failed to import name.");
		break;
	}

	major = gss_compare_name(&minor,
	                          gh->target,
	                          tname,
	                         &equal);

			
	if (major != GSS_S_COMPLETE)
		ec = ec_create(major, 
		                 minor,
		                "Failed to compare names.");

	gss_release_name(&minor, &tname);
	if (ec)
		return ec;

	if (equal)
		return ec;

	major = gss_display_name(&minor,
	                          gh->target,
	                         &tbuf,
	                          NULL);
	if (major != GSS_S_COMPLETE)
		return ec_create(major, 
		                 minor,
		                "Failed to get display name.");

	name = Strndup(tbuf.value, tbuf.length);
	ec = ec_create(EC_GSI_SUCCESS,
	               EC_GSI_SUCCESS,
	               "The remote service's identity is wrong: %s",
	               name);

	Free(name);
	gss_release_buffer(&minor, &tbuf);
	return ec;
}


errcode_t
_g_gen_token(gh_t * gh, int init)
{
	errcode_t ec    = EC_SUCCESS;
	gss_buffer_desc itoken = GSS_C_EMPTY_BUFFER;
	gss_buffer_desc otoken = GSS_C_EMPTY_BUFFER;
	gss_buffer_desc tbuf   = GSS_C_EMPTY_BUFFER;
	OM_uint32 major;
	OM_uint32 minor;
	OM_uint32 flags = 0;

	if (gh->creds == GSS_C_NO_CREDENTIAL)
	{
		ec = _g_acquire_cred(&gh->creds);
		if (ec)
			return ec;

		if (init)
		{
			switch(s_dcau())
			{
			case 0: /* None. Will never happen. */
				assert(0);
			case 1: /* Self. */
				major = gss_inquire_cred(&minor,
				                          gh->creds,
				                         &gh->target,
		   		                       NULL,
		       		                   NULL,
		           		               NULL);
				if (major != GSS_S_COMPLETE)
					return ec_create(major, 
					                 minor,
					                "Failed to inquire credential.");
				break;

			case 2: /* Subject. */
				tbuf.value = s_dcau_subject();
				tbuf.length = strlen(tbuf.value);
				major = gss_import_name(&minor,
				                        &tbuf,
				                         GSS_C_NO_OID,
				                        &gh->target);
				Free(tbuf.value);
				if (major != GSS_S_COMPLETE)
					return ec_create(major, 
					                 minor,
					                "Failed to import name.");
				break;
			}
		}
	}

	switch (s_prot())
	{
	case 0:
		flags = GSS_C_MUTUAL_FLAG;
		break;
	case 1:
		flags = GSS_C_MUTUAL_FLAG|GSS_C_INTEG_FLAG|GSS_C_GLOBUS_SSL_COMPATIBLE;
		break;
	case 2:
		flags = GSS_C_MUTUAL_FLAG|GSS_C_CONF_FLAG|GSS_C_GLOBUS_SSL_COMPATIBLE;
		break;
	case 3:
		flags = GSS_C_MUTUAL_FLAG|
		        GSS_C_CONF_FLAG|
		        GSS_C_INTEG_FLAG|
		        GSS_C_GLOBUS_SSL_COMPATIBLE;
		break;
	}

	itoken.value  = gh->buf;
	itoken.length = gh->cnt;
	if (init)
	{
		major = gss_init_sec_context(&minor,
		                              gh->creds,
		                             &gh->cntxt,
		                              gh->target,
		                              GSS_C_NO_OID,
		                              flags,
		                              0,
		                              GSS_C_NO_CHANNEL_BINDINGS,
		                             &itoken,
		                              NULL,
		                             &otoken,
		                              NULL,
		                              NULL);

		if (major != GSS_S_COMPLETE && major != GSS_S_CONTINUE_NEEDED)
			return ec_create(major, 
			                 minor,
			                "Failed to init security context.");
	} else
	{
		major = gss_accept_sec_context(&minor,
		                               &gh->cntxt,
		                                gh->creds,
		                               &itoken,
		                                GSS_C_NO_CHANNEL_BINDINGS,
		                               &gh->target,
		                                NULL,
		                               &otoken,
		                               &flags,
		                                NULL,
		                                NULL);

		if (major != GSS_S_COMPLETE && major != GSS_S_CONTINUE_NEEDED)
			return ec_create(major, 
			                 minor,
			                "Failed to accept security context.");
	}

	if (major == GSS_S_COMPLETE)
	{
		/* Compare targets. */
		if (!init)
			ec = _g_compare_names(gh);
		if (ec)
			return ec;

		/* Get the buffer limit */
		ec = gsi_pbsz_maxumsg(gh, gh->pbsz, &gh->upbsz);
		if (ec)
			return ec;

		if (gh->upbsz <= 0)
			return ec_create(EC_GSI_SUCCESS,
			                 EC_GSI_SUCCESS,
			                 "Given PBSZ size (%d) is too small.\n",
			                 gh->pbsz);

		gh->done = 1;
	}

	FREE(gh->buf);
	gh->cnt = gh->len = 0;

	gh->state = G_S_READ; /* read */
	if (otoken.length)
	{
		gh->buf = otoken.value;
		gh->len = gh->cnt = otoken.length;
		gh->state = G_S_WRITE; /* Write */
	}

	return ec;
}

errcode_t
_g_send_token(gh_t * gh, nh_t * nh)
{
	errcode_t ec = EC_SUCCESS;
	size_t off   = gh->len - gh->cnt;

	ec = net_write_nb(nh, gh->buf + off, &gh->cnt);

	if (!gh->cnt)
	{
		FREE(gh->buf);
		gh->buf = NULL;
		gh->cnt = gh->len = 0;
		gh->state = G_S_READ; /* read */
	}

	return ec;
}

errcode_t
_g_recv_token(gh_t * gh, nh_t * nh)
{
	errcode_t ec    = EC_SUCCESS;
	size_t    count = 0;
	int       eof   = 0;

	if (gh->len < 5)
	{
		gh->len = 5;
		gh->buf = (char *) realloc(gh->buf, gh->len);
	}

	if (gh->cnt < 5)
	{
		count = 5 - gh->cnt;
		ec = net_read(nh, gh->buf + gh->cnt, &count, &eof);
		if (ec)
			return ec;
		gh->cnt += count;
	}

	if (gh->cnt < 5)
		return ec;

	/* SSL Header:
	 *  byte 0: 20-26
	 *  byte 1: 3
	 *  byte 2: 0|1
	 *  byte 3: MSB length
	 *  byte 4: LSB length
	 */
	count = SSL_TOK_LEN(gh->buf) + 5;
	if (gh->len < count)
	{
		gh->len = count;
		gh->buf = (char *) realloc(gh->buf, gh->len);
	}

	count -= gh->cnt;
	ec = net_read(nh, gh->buf + gh->cnt, &count, &eof);
	if (!ec)
		gh->cnt += count;

	if (!ec && gh->cnt == gh->len)
		gh->state = G_S_GEN; /* Gen */

	return ec;
}

errcode_t
_g_acquire_cred(gss_cred_id_t * credp)
{
	OM_uint32       major;
	OM_uint32       minor;
	static gss_buffer_desc exported_creds = {0, NULL};
#if defined MSSFTP && defined MSSFTP_GSI_SERVICE
	gss_buffer_desc input_name_buffer;
	gss_name_t      target;
#endif /* MSSFTP && MSSFTP_GSI_SERVICE */

	if (exported_creds.length)
	{
		major = gss_import_cred (&minor,
		                          credp,
		                          NULL,
		                          0,
		                         &exported_creds,
		                          0,
		                          0);

		if (major == GSS_S_COMPLETE)
			return EC_SUCCESS;
	}

#ifdef MSSFTP
#ifdef MSSFTP_GSI_SERVICE
XXX Fix for local hostname
	input_name_buffer.value  = Sprintf(NULL, "mssftp@blitzkrieg.ncsa.uiuc.edu");
	input_name_buffer.length = strlen(input_name_buffer.value) + 1;
	major = gss_import_name(&minor,
	                        &input_name_buffer,
	                         GSS_C_NT_HOSTBASED_SERVICE,
	                        &target);

	FREE(input_name_buffer.value);

	if (major != GSS_S_COMPLETE)
	{
		return ec_create(major,
		                minor,
		                "Failed to import service name");
	}
#else /* MSSFTP_GSI_SERVICE */
	/*
	 * If we are doing mssftp but we do not have the GSI service functionality,
	 * force discovery of the certificate in 
	 * /etc/grid-security/mssftp/mssftpcert.pem.
	 */
	putenv("X509_USER_KEY=/etc/grid-security/mssftp/mssftpkey.pem");
	putenv("X509_USER_CERT=/etc/grid-security/mssftp/mssftpcert.pem");
#endif /* MSSFTP_GSI_SERVICE */
#endif /* MSSFTP */

	major = gss_acquire_cred(&minor,
#if defined MSSFTP && defined MSSFTP_GSI_SERVICE
	                          target,
#else /* MSSFTP && MSSFTP_GSI_SERVICE */
	                          GSS_C_NO_NAME,
#endif /* MSSFTP && MSSFTP_GSI_SERVICE */
			                  0,
			                  GSS_C_NULL_OID_SET,
			                  GSS_C_BOTH,
			                  credp,
			                  NULL,
			                  NULL);

	if (major != GSS_S_COMPLETE)
		return ec_create(major, 
		                 minor,
		                 "Failed to acquire credentials.");

	if (exported_creds.length)
		gss_release_buffer(&minor, &exported_creds);
	exported_creds.length = 0;

	major = gss_export_cred(&minor,
	                        *credp,
	                         NULL,
	                         0,
	                        &exported_creds);

	return EC_SUCCESS;
}
