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
#include <sys/socket.h>
#include <sys/stat.h>
#include <fnmatch.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>

#include "linterface.h"
#include "settings.h"
#include "errcode.h"
#include "network.h"
#include "output.h"
#include "misc.h"
#include "ftp.h"
#include "gsi.h"
#include "ftp_eb.h"
#include "ftp_s.h"
#include "ftp_a.h"

#ifdef DMALLOC
#include "dmalloc.h"
#endif /* DMALLOC */

#define F_CODE_CONT(x) ((x) <  200)
#define F_CODE_COMP(x) ((x) >= 200)
#define F_CODE_SUCC(x) ((x) >= 200 && (x) < 299)
#define F_CODE_ERR(x)  ((x) >= 400)
#define F_CODE_TRANS_ERR(x) ((x) >= 400 && (x) <= 499)
#define F_CODE_FINAL_ERR(x) ((x) >= 500 && (x) <= 599)
#define F_CODE_INTR(x)      ((x) >= 300 && (x) <= 399)
#define F_CODE_UNKNOWN(x)  (((x) >= 500 && (x) <= 509) || (x) == 202)


typedef struct ftp_handle {
	int    port;
	char * host;
	char * rhost;
	char * user;
	char * pass;

	struct {
		nh_t * nh;
		gh_t * gh;
		int    eof;
		char * buf;
		size_t len;
		size_t cnt;
	} cc; /* Control channel. */

	dch_t dcs; /* Data channels. */

	/* Supported Features */
	int hasChgrp;
	int hasDcau;
	int hasCksum;
	int hasParallel;
	int hasMlst;
	int hasEsto;
	int hasEret;
	int hasFamily;
	int hasPasv;
	int hasAllo;
	int hasSize;
	int hasWind;
	int hasSbuf;
	int hasSiteBufsize;
	int hasStage;

	/* Mlsx features */
	mf_t mf;

	/* Transfer setup */
	int stream; /* 0 stream, 1 eb */
	int ascii;  /* 0 binary, 1 ascii */

	/* Connection information */
	struct sockaddr_in * sinp;
	int scnt;

	/* Flag to indicate that we are expecting a response. */
	int rsp;

	char * ocwd; /* Previous working directory. */
	char * cwd;  /* Current working directory. */

	time_t keepalive;

} fh_t;


static errcode_t
_f_send_cmd(fh_t * fh, char * cmd);

static errcode_t
_f_prep_dc(fh_t * fh, 
           fh_t * ofh, 
           int    stream,
           int    binary,
           int    retr);

static errcode_t
_f_poll_resp(fh_t * fh, int * code, char ** resp);

static errcode_t
_f_push_resp(fh_t * fh, char * resp);

static errcode_t
_f_peak_resp(fh_t * fh, int * code);

static errcode_t
_f_get_final_resp(fh_t * fh, int * code, char ** resp);

static errcode_t
_f_get_resp(fh_t * fh, int * code, char ** resp);

static errcode_t
_f_setup_tcp(fh_t * fh, fh_t * ofh);

static errcode_t
_f_setup_mode(fh_t * fh, fh_t * ofh, int stream);

static errcode_t
_f_setup_dcau(fh_t * fh, fh_t * ofh);

static errcode_t
_f_setup_pbsz(fh_t * fh, fh_t * ofh);

static errcode_t
_f_setup_prot(fh_t * fh, fh_t * ofh);

static errcode_t
_f_setup_type(fh_t * fh, fh_t * ofh, int binary);

static errcode_t
_f_setup_dci(fh_t * fh, fh_t * ofh);

static errcode_t
_f_setup_spor(fh_t * fh, fh_t * ofh, int retr);

static errcode_t
_f_setup_spas(fh_t * fh, fh_t * ofh, int retr);

static errcode_t
_f_setup_port(fh_t * fh, fh_t * ofh, int retr);

static errcode_t
_f_setup_pasv(fh_t * fh, fh_t * ofh, int retr);

static errcode_t
_f_setup_conntype(fh_t * fh, fh_t * ofh, int retr);

static int
_f_parse_addrs(
    char               *  str,
    struct sockaddr_in ** sinp);

static void
_f_mlsx(char * str, ml_t * ml);

static errcode_t
_f_keepalive(fh_t * fh);

static void
_f_destroy_pd(pd_t * pd);

static errcode_t
_f_connect(fh_t * fh, char ** srvrmsg);

static errcode_t
_f_reconnect(fh_t * fh);

static errcode_t
_f_chdir(fh_t * fh, char * path);

static errcode_t
_f_readdir_mlsd(pd_t * pd, char * path, ml_t *** mlp, char * token);

static errcode_t
_f_readdir_nlst(pd_t * pd, char * path, ml_t *** mlp, char * token);

static errcode_t
ftp_connect(pd_t *  pd, 
            char *  host, 
            int     port, 
            char *  user, 
            char *  pass, 
            char ** srvrmsg)
{
	errcode_t   ec    = EC_SUCCESS;
	char      * resp  = NULL;
	char      * cptr  = NULL;
	char      * eor   = NULL;
	char      * eol   = NULL;
	fh_t      * fh    = NULL;
	int         code  = 0;

	/* Check for a current connection. */
	if (pd->ftppriv)
		return ec_create(EC_GSI_SUCCESS,
		                 EC_GSI_SUCCESS,
		                 "Already connected.");

	/* Set us up the handle. */
	pd->ftppriv = fh = (fh_t *) malloc(sizeof(fh_t));
	memset(fh, 0, sizeof(fh_t));

	fh->host = Strdup(host);
	fh->user = Strdup(user);
	fh->pass = Strdup(pass);
	fh->port = port;
	fh->rsp  = 1; /* MOTD */

	ec = _f_connect(fh, srvrmsg);
	if (ec)
		goto cleanup;

	/* Set the default features. */
    fh->hasChgrp  = 1;
    fh->hasFamily = 1;
    fh->hasCksum  = 1;
    fh->hasPasv   = 1;
    fh->hasAllo   = 1;
    fh->hasSize   = 1;
    fh->hasWind   = 1;
    fh->hasSbuf   = 1;
    fh->hasSiteBufsize = 1;
    fh->hasStage   = 1;

	/* Now grab the feature set, if available. */
	ec = _f_send_cmd(fh, "FEAT");
	if (ec != EC_SUCCESS)
		goto cleanup;

	ec = _f_get_final_resp(fh, &code, &resp);
	if (ec != EC_SUCCESS || F_CODE_ERR(code) || !resp)
		goto cleanup;

	cptr = resp;

	if (strstr(cptr, "\r\n DCAU\r\n"))
		fh->hasDcau = 1;

	if (strstr(cptr, "\r\n ESTO\r\n"))
		fh->hasEsto = 1;

	if (strstr(cptr, "\r\n ERET\r\n"))
		fh->hasEret = 1;

	if (strstr(cptr, "\r\n PARALLEL\r\n"))
		fh->hasParallel = 1;

	if ((cptr = strstr(cptr, "\r\n MLST ")) != NULL)
	{
		fh->hasMlst = 1;

		cptr += 8;
		eol = strstr(cptr, "\r\n");
		if (eol)
			*eol = '\0';

		while (cptr && *cptr)
		{
			if (strncasecmp("type", cptr, 4) == 0)
				fh->mf.Type = 1;
			else if (strncasecmp("size", cptr, 4) == 0)
				fh->mf.Size = 1;
			else if (strncasecmp("modify", cptr, 6) == 0)
				fh->mf.Modify = 1;
			else if (strncasecmp("perm", cptr, 4) == 0)
				fh->mf.Perm = 1;
			else if (strncasecmp("charset", cptr, 4) == 0)
				fh->mf.Charset = 1;
			else if (strncasecmp("UNIX.mode", cptr, 9) == 0)
				fh->mf.UNIX_mode = 1;
			else if (strncasecmp("UNIX.owner", cptr, 10) == 0)
				fh->mf.UNIX_owner = 1;
			else if (strncasecmp("UNIX.group", cptr, 10) == 0)
				fh->mf.UNIX_group = 1;
			else if (strncasecmp("unique", cptr, 6) == 0)
				fh->mf.Unique = 1;
			else if (strncasecmp("UNIX.slink", cptr, 10) == 0)
				fh->mf.UNIX_slink = 1;
			else if (strncasecmp("X.family", cptr, 8) == 0)
				fh->mf.X_family = 1;
			else if (strncasecmp("X.archive", cptr, 9) == 0)
				fh->mf.X_archive = 1;

			eor = strchr(cptr, ';');
			if (!eor)
				eor = cptr + strlen(cptr) - 1;
			cptr = eor + 1;
		}

		if (eol)
			*eol = '\r';
	}

cleanup:
	FREE(resp);

	if (ec != EC_SUCCESS)
		_f_destroy_pd(pd);

	return ec;
}

static int 
ftp_connected(pd_t * pd)
{
	return 1;
}

static errcode_t
ftp_disconnect(pd_t *  pd, 
               char ** msg)
{
	int    code  = 0;
	fh_t * fh    = (fh_t*)pd->ftppriv;
	errcode_t ec = EC_SUCCESS;

	*msg = NULL;

	if (net_connected(fh->cc.nh))
	{
		ec = _f_send_cmd(fh, "QUIT");
		if (ec != EC_SUCCESS)
			goto finish;

		ec = _f_get_final_resp(fh, &code, msg);
		if (ec != EC_SUCCESS)
			goto finish;
	}

finish:
	net_close(fh->cc.nh);
	_f_destroy_pd(pd);

	return ec;
}

static errcode_t
ftp_retr(pd_t * pd, 
         pd_t * opd, 
         char * file, 
         globus_off_t off, 
         globus_off_t len)
{
	int       code = 0;
	fh_t    * fh   = (fh_t *) pd->ftppriv;
	fh_t    * ofh  = NULL;
	char    * cmd  = NULL;
	char    * resp = NULL;
	errcode_t ec = EC_SUCCESS;

	/* Reconnect */
	ec = _f_reconnect(fh);
	if (ec)
		return ec;

	if (opd)
		ofh = (fh_t *) opd->ftppriv;
	fh->dcs.off = off;
	if (off == (globus_off_t)-1)
		fh->dcs.off = 0;

	if (len != -1 && !fh->hasEret)
		return ec_create(EC_GSI_SUCCESS,
		                 EC_GSI_SUCCESS,
		                 "Remote service does not support partial retrieves");

	ec = _f_prep_dc(fh, 
	                ofh, 
	                0, 
	                len == -1 ? 0 : 1, /* Force binary mode. */
	                1);
	if (ec)
		return ec;

	if (!fh->stream)
	{
		cmd = Sprintf(NULL, 
		              "OPTS RETR Parallelism=%d,%d,%d;",
		              s_parallel(),
		              s_parallel(),
		              s_parallel());

		ec = _f_send_cmd(fh, cmd);
		FREE(cmd);
		if (ec)
			return ec;

		ec = _f_get_final_resp(fh, &code, &resp);
		if (!ec)
			return ec;

		if (!F_CODE_SUCC(code))
			ec = ec_create(EC_GSI_SUCCESS,
			               EC_GSI_SUCCESS,
			               "Failed to setup desired parallelism:\n%s",
			               resp);
		FREE(resp);
		if (ec)
			return ec;
	}

	if (off == (globus_off_t)-1)
		cmd = Sprintf(cmd, "RETR %s", file);
	else
		cmd = Sprintf(
		         cmd, 
		         "ERET P %" GLOBUS_OFF_T_FORMAT " %" GLOBUS_OFF_T_FORMAT " %s",
		         off,
		         len,
		         file);

	ec = _f_send_cmd(fh, cmd);

	FREE(cmd);
	fh->keepalive = time(NULL);
	return ec;
}

static errcode_t
ftp_stor(pd_t * pd, 
         pd_t * opd, 
         char * file, 
         int    unique,
         globus_off_t off, 
         globus_off_t len)
{
	int       code = 0;
	fh_t    * fh   = (fh_t *) pd->ftppriv;
	fh_t    * ofh  = NULL;
	char    * cmd  = NULL;
	char    * resp = NULL;
	errcode_t ec = EC_SUCCESS;

	/* Reconnect */
	ec = _f_reconnect(fh);
	if (ec)
		return ec;

	if (opd)
		ofh = (fh_t *) opd->ftppriv;

	if (off != (globus_off_t)-1 && !fh->hasEret)
		return ec_create(EC_GSI_SUCCESS,
		                 EC_GSI_SUCCESS,
		                 "Remote service does not support partial stores");

	/* Send allo */
	if (fh->hasAllo && len != -1 && off == (globus_off_t)-1)
	{
		cmd = Sprintf(NULL, "ALLO %" GLOBUS_OFF_T_FORMAT, len);
		ec  = _f_send_cmd(fh, cmd);
		FREE(cmd);
		if (ec)
			return ec;

		ec = _f_get_final_resp(fh, &code, &resp);
		if (ec)
			return ec;

		if (F_CODE_UNKNOWN(code))
			fh->hasAllo = 0;
		else if (!F_CODE_SUCC(code))
			ec = ec_create(EC_GSI_SUCCESS,
			               EC_GSI_SUCCESS,
			               "Failed to send allo:\n%s",
			               resp);
		FREE(resp);
		if (ec)
			return ec;
	}

	/* Set family. */
	if (fh->hasFamily && s_family())
	{
		cmd = Sprintf(NULL, "SETFAM %s", s_family());
		ec  = _f_send_cmd(fh, cmd);
		FREE(cmd);
		if (ec)
			return ec;

		ec = _f_get_final_resp(fh, &code, &resp);
		if (ec)
			return ec;

		if (F_CODE_UNKNOWN(code))
			fh->hasFamily = 0;
		else if (!F_CODE_SUCC(code))
			ec = ec_create(EC_GSI_SUCCESS,
			               EC_GSI_SUCCESS,
			               "Failed to setup desired family:\n%s",
			               resp);
		FREE(resp);
		if (ec)
			return ec;
	}

	/* Send ALLO */
	if (fh->hasAllo)
	{
	}

	ec = _f_prep_dc(fh, 
	                ofh, 
	                0, 
	                len == -1 ? 0 : 1, /* Force binary mode. */
	                0);

	if (ec)
		return ec;

	if (off == (globus_off_t)-1 && unique)
		cmd = Sprintf(cmd, "STOU %s", file);
	else if (off == (globus_off_t)-1)
		cmd = Sprintf(cmd, "STOR %s", file);
	else
		cmd = Sprintf(
		         cmd, 
		         "ESTO A %" GLOBUS_OFF_T_FORMAT " %s",
		         off,
		         file);

	ec = _f_send_cmd(fh, cmd);
	FREE(cmd);
	fh->keepalive = time(NULL);
	return ec;
}

static errcode_t
ftp_read(pd_t          *  pd,
         char          ** buf,
         globus_off_t  *  off,
         size_t        *  len,
         int           *  eof)
{
	int         code  = 0;
	int         ready = 0;
	fh_t      * fh    = (fh_t *) pd->ftppriv;
	char      * resp  = NULL;
	errcode_t   ec    = EC_SUCCESS;

	*buf = NULL;
	*len = 0;
	*eof = 0;

	do {
		ec = _f_keepalive(fh);
		if (ec)
			return ec;

		/* Check the control channel for messages. */
		ec = _f_poll_resp(fh, &code, &resp);
		if (ec)
			return ec;

		if (F_CODE_ERR(code))
		{
			ec = ec_create(EC_GSI_SUCCESS,
			               EC_GSI_SUCCESS,
			               "%s",
			               resp);
			if (F_CODE_TRANS_ERR(code))
				ec_set_flag(ec, EC_FLAG_CAN_RETRY);
			FREE(resp);
			return ec;
		}
		FREE(resp);

		if (!fh->dcs.dci.read)
		{
			if (F_CODE_SUCC(code))
				*eof = 1;
			return ec;
		}
	} while (!(ec = fh->dcs.dci.read_ready(&fh->dcs, &ready)) && !ready);
	if (ec)
		return ec;

	return fh->dcs.dci.read(&fh->dcs, buf, off, len, eof);
}

static errcode_t
ftp_write(pd_t          * pd,
          char          * buf,
          globus_off_t    off,
          size_t          len,
          int             eof)
{
	int         code  = 0;
	int         ready = 0;
	fh_t      * fh    = (fh_t *) pd->ftppriv;
	char      * resp  = NULL;
	errcode_t   ec    = EC_SUCCESS;

	do {
		ec = _f_keepalive(fh);
		if (ec)
		{
			FREE(buf);
			return ec;
		}

		/* Check the control channel for messages. */
		ec = _f_poll_resp(fh, &code, &resp);
		if (ec != EC_SUCCESS)
		{
			FREE(buf);
			return ec;
		}

		if (F_CODE_ERR(code))
		{
			ec = ec_create(EC_GSI_SUCCESS,
			               EC_GSI_SUCCESS,
			               "%s",
			               resp);
			if (F_CODE_TRANS_ERR(code))
				ec_set_flag(ec, EC_FLAG_CAN_RETRY);

			FREE(resp);
			FREE(buf);
			return ec;
		}
		FREE(resp);

		if (!fh->dcs.dci.write)
			return ec;
	} while (!(ec = fh->dcs.dci.write_ready(&fh->dcs, &ready)) && !ready);
	if (ec)
		return ec;

	return fh->dcs.dci.write(&fh->dcs, buf, off, len, eof);
}

static errcode_t
ftp_close(pd_t * pd)
{
	fh_t    * fh = (fh_t *) pd->ftppriv;
	errcode_t ec = EC_SUCCESS;
	int    code    = 0;
	int    retcode = 0;
	char * resp    = NULL;
	char * retresp = NULL;

	if (fh->sinp)
	{
/* XXX 3rd party stuff */
	}

	if (fh->dcs.dci.close)
		fh->dcs.dci.close(&fh->dcs);

	FREE(fh->sinp);
	fh->sinp = NULL;

	if (net_connected(fh->cc.nh))
	{
		ec = _f_get_final_resp(fh, &retcode, &retresp);
		if (ec == EC_SUCCESS)
		{
			/* Soak up the keepalive NOOP responses. */
			while ((ec = _f_get_final_resp(fh, &code, &resp)) == EC_SUCCESS)
			{
				if (!code)
					break;
				FREE(resp);
			}
		}
	
		if (ec == EC_SUCCESS && F_CODE_ERR(retcode))
		{
			ec = ec_create(EC_GSI_SUCCESS,
			               EC_GSI_SUCCESS,
			               "%s",
			               retresp);
			if (F_CODE_TRANS_ERR(retcode))
				ec_set_flag(ec, EC_FLAG_CAN_RETRY);
		}
		FREE(retresp);
	}
	fh->keepalive = 0;
	memset(&fh->dcs.dci, 0, sizeof(dci_t));

	return ec;
}


static errcode_t
ftp_list(pd_t * pd, char * path)
{
	fh_t    * fh  = (fh_t *) pd->ftppriv;
	char    * cmd = NULL;
	errcode_t ec  = EC_SUCCESS;

	/* Reconnect */
	ec = _f_reconnect(fh);
	if (ec)
		return ec;

	ec = _f_prep_dc(fh, NULL, 1, 1, 1);
	if (ec)
		return ec;

	cmd = Sprintf(cmd, "LIST%s%s", path ? " ": "", path ? path : "");
	ec = _f_send_cmd(fh, cmd);
	FREE(cmd);
	fh->keepalive = time(NULL);
	return ec;
}

static errcode_t
ftp_pwd(pd_t * pd, char ** path)
{
	int         code = 0;
	fh_t      * fh   = (fh_t *) pd->ftppriv;
	char      * resp = NULL;
	errcode_t   ec   = EC_SUCCESS;

	*path = NULL;

	/* Reconnect */
	ec = _f_reconnect(fh);
	if (ec)
		return ec;

	ec = _f_send_cmd(fh, "PWD");
	if (ec != EC_SUCCESS)
		goto cleanup;

	ec = _f_get_final_resp(fh, &code, &resp);
	if (ec != EC_SUCCESS)
		goto cleanup;

	if (F_CODE_INTR(code))
		ec = ec_create(EC_GSI_SUCCESS,
		               EC_GSI_SUCCESS,
		               "Unexpected response to pwd: %s",
		               resp);

	if (F_CODE_ERR(code))
	{
		ec = ec_create(EC_GSI_SUCCESS,
		               EC_GSI_SUCCESS,
		               "%s",
		               resp);
		if (F_CODE_TRANS_ERR(code))
			ec_set_flag(ec, EC_FLAG_CAN_RETRY);
	}

	if (code == 257)
		*path = DupQuotedString(resp);

cleanup:
	FREE(resp);
	return ec;
}


static errcode_t
ftp_chdir(pd_t * pd, char * path)
{
	char            * cmd  = NULL;
	char            * resp = NULL;
	fh_t            * fh   = (fh_t *) pd->ftppriv;
	errcode_t         ec   = EC_SUCCESS;

	/* Reconnect */
	ec = _f_reconnect(fh);
	if (ec)
		return ec;

	if (path && strcmp(path, "-") == 0)
	{
		if (!fh->ocwd)
			return ec_create(EC_GSI_SUCCESS,
			                 EC_GSI_SUCCESS,
			                 "OLDPWD not set.");
		path = fh->ocwd;
	}

	if (!fh->cwd)
		ec = ftp_pwd(pd, &fh->cwd);
	if (ec)
		return ec;

	ec = _f_chdir(fh, path);

	if (!ec)
	{
		FREE(fh->ocwd);
		fh->ocwd = fh->cwd;

		/* Failure here could break operations after reconnect. */
		ec = ftp_pwd(pd, &fh->cwd);
	}

	FREE(cmd);
	FREE(resp);
	return ec;
}

static errcode_t
ftp_chgrp(pd_t * pd, char * group, char * path)
{
	int               code = 0;
	char            * cmd  = NULL;
	char            * resp = NULL;
	fh_t            * fh   = (fh_t *) pd->ftppriv;
	errcode_t         ec   = EC_SUCCESS;

	/* Reconnect */
	ec = _f_reconnect(fh);
	if (ec)
		return ec;

	if (fh->hasChgrp)
	{
		cmd = Sprintf(cmd, "SITE CHGRP %s %s", group, path);
		ec = _f_send_cmd(fh, cmd);
		if (ec != EC_SUCCESS)
			goto cleanup;

		ec = _f_get_final_resp(fh, &code, &resp);
		if (ec != EC_SUCCESS)
			goto cleanup;

		if (F_CODE_INTR(code))
			ec = ec_create(EC_GSI_SUCCESS,
			               EC_GSI_SUCCESS,
			               "Unexpected response to chgrp: %s",
			               resp);

		/* 504 is unimplemented but in this case, it's invalid group. */
		if (F_CODE_ERR(code) && (!F_CODE_UNKNOWN(code) || code == 504))
		{
			ec = ec_create(EC_GSI_SUCCESS,
			               EC_GSI_SUCCESS,
			               "%s",
			               resp);
			if (F_CODE_TRANS_ERR(code))
				ec_set_flag(ec, EC_FLAG_CAN_RETRY);
		}
	}

	if (!fh->hasChgrp || (F_CODE_UNKNOWN(code) && code != 504))
	{
		ec = ec_create(EC_GSI_SUCCESS,
		               EC_GSI_SUCCESS,
		               "Remote service does not support group changing.");
		fh->hasChgrp = 0;
	}

cleanup:
	FREE(cmd);
	FREE(resp);
	return ec;
}

static errcode_t
ftp_chmod(pd_t * pd, int perms, char * path)
{
	int               code = 0;
	char            * cmd  = NULL;
	char            * resp = NULL;
	fh_t            * fh   = (fh_t *) pd->ftppriv;
	errcode_t         ec   = EC_SUCCESS;

	/* Reconnect */
	ec = _f_reconnect(fh);
	if (ec)
		return ec;

	cmd = Sprintf(cmd, "SITE CHMOD %o %s", perms, path);
	ec = _f_send_cmd(fh, cmd);
	if (ec != EC_SUCCESS)
		goto cleanup;

	ec = _f_get_final_resp(fh, &code, &resp);
	if (ec != EC_SUCCESS)
		goto cleanup;

	if (F_CODE_INTR(code))
		ec = ec_create(EC_GSI_SUCCESS,
		               EC_GSI_SUCCESS,
		               "Unexpected response to chmod: %s",
		               resp);

	if (F_CODE_ERR(code))
	{
		ec = ec_create(EC_GSI_SUCCESS,
		               EC_GSI_SUCCESS,
		               "%s",
		               resp);
		if (F_CODE_TRANS_ERR(code))
			ec_set_flag(ec, EC_FLAG_CAN_RETRY);
	}

cleanup:
	FREE(cmd);
	FREE(resp);
	return ec;
}

static errcode_t
ftp_mkdir(pd_t * pd, char * path)
{
	int               code = 0;
	char            * cmd  = NULL;
	char            * resp = NULL;
	fh_t            * fh   = (fh_t *) pd->ftppriv;
	errcode_t         ec   = EC_SUCCESS;

	/* Reconnect */
	ec = _f_reconnect(fh);
	if (ec)
		return ec;

	cmd = Sprintf(cmd, "MKD %s", path);
	ec = _f_send_cmd(fh, cmd);
	if (ec != EC_SUCCESS)
		goto cleanup;

	ec = _f_get_final_resp(fh, &code, &resp);
	if (ec != EC_SUCCESS)
		goto cleanup;

	if (F_CODE_INTR(code))
		ec = ec_create(EC_GSI_SUCCESS,
		               EC_GSI_SUCCESS,
		               "Unexpected response to mkdir: %s",
		               resp);

	if (F_CODE_ERR(code))
	{
		ec = ec_create(EC_GSI_SUCCESS,
		               EC_GSI_SUCCESS,
		               "%s",
		               resp);
		if (F_CODE_TRANS_ERR(code))
			ec_set_flag(ec, EC_FLAG_CAN_RETRY);
	}

cleanup:
	FREE(cmd);
	FREE(resp);
	return ec;
}

static errcode_t
ftp_rename(pd_t * pd, char * old, char * new)
{
	int         code = 0;
	fh_t      * fh   = (fh_t *) pd->ftppriv;
	char      * cmd  = NULL;
	char      * resp = NULL;
	errcode_t   ec   = EC_SUCCESS;

	/* Reconnect */
	ec = _f_reconnect(fh);
	if (ec)
		return ec;

	cmd = Sprintf(cmd, "RNFR %s", old);
	ec = _f_send_cmd(fh, cmd);
	if (ec != EC_SUCCESS)
		goto cleanup;

	ec = _f_get_final_resp(fh, &code, &resp);

	if (F_CODE_ERR(code))
	{
		ec = ec_create(EC_GSI_SUCCESS,
		               EC_GSI_SUCCESS,
		               "%s",
		               resp);
		if (F_CODE_TRANS_ERR(code))
			ec_set_flag(ec, EC_FLAG_CAN_RETRY);
	}

	if (F_CODE_SUCC(code))
		ec = ec_create(EC_GSI_SUCCESS,
		               EC_GSI_SUCCESS,
		               "Unexpected response to RNFR: %s",
		               resp);

	if (ec != EC_SUCCESS)
		goto cleanup;

	cmd = Sprintf(cmd, "RNTO %s", new);
	ec  = _f_send_cmd(fh, cmd);
	if (ec != EC_SUCCESS)
		goto cleanup;

	FREE(resp);
	resp = NULL;
	ec = _f_get_final_resp(fh, &code, &resp);

	if (F_CODE_ERR(code))
	{
		ec = ec_create(EC_GSI_SUCCESS,
		               EC_GSI_SUCCESS,
		               "%s",
		               resp);
		if (F_CODE_TRANS_ERR(code))
			ec_set_flag(ec, EC_FLAG_CAN_RETRY);
	}

	if (F_CODE_INTR(code))
		ec = ec_create(EC_GSI_SUCCESS,
		               EC_GSI_SUCCESS,
		               "Unexpected response to RNTO: %s",
		               resp);

cleanup:
	FREE(cmd);
	FREE(resp);
	return ec;
}

static errcode_t
ftp_rm(pd_t * pd, char * path)
{
	int               code = 0;
	char            * cmd  = NULL;
	char            * resp = NULL;
	fh_t            * fh   = (fh_t *) pd->ftppriv;
	errcode_t         ec   = EC_SUCCESS;

	/* Reconnect */
	ec = _f_reconnect(fh);
	if (ec)
		return ec;

	cmd = Sprintf(cmd, "DELE %s", path);
	ec = _f_send_cmd(fh, cmd);
	if (ec != EC_SUCCESS)
		goto cleanup;

	ec = _f_get_final_resp(fh, &code, &resp);
	if (ec != EC_SUCCESS)
		goto cleanup;

	if (F_CODE_INTR(code))
		ec = ec_create(EC_GSI_SUCCESS,
		               EC_GSI_SUCCESS,
		               "Unexpected response to dele: %s",
		               resp);

	if (F_CODE_ERR(code))
	{
		ec = ec_create(EC_GSI_SUCCESS,
		               EC_GSI_SUCCESS,
		               "%s",
		               resp);
		if (F_CODE_TRANS_ERR(code))
			ec_set_flag(ec, EC_FLAG_CAN_RETRY);
	}

cleanup:
	FREE(cmd);
	FREE(resp);
	return ec;
}

static errcode_t
ftp_rmdir(pd_t * pd, char * path)
{
	int               code = 0;
	char            * cmd  = NULL;
	char            * resp = NULL;
	fh_t            * fh   = (fh_t *) pd->ftppriv;
	errcode_t         ec   = EC_SUCCESS;

	/* Reconnect */
	ec = _f_reconnect(fh);
	if (ec)
		return ec;

	cmd = Sprintf(cmd, "RMD %s", path);
	ec = _f_send_cmd(fh, cmd);
	if (ec != EC_SUCCESS)
		goto cleanup;

	ec = _f_get_final_resp(fh, &code, &resp);
	if (ec != EC_SUCCESS)
		goto cleanup;

	if (F_CODE_INTR(code))
		ec = ec_create(EC_GSI_SUCCESS,
		               EC_GSI_SUCCESS,
		               "Unexpected response to rmdir: %s",
		               resp);

	if (F_CODE_ERR(code))
	{
		ec = ec_create(EC_GSI_SUCCESS,
		               EC_GSI_SUCCESS,
		               "%s",
		               resp);
		if (F_CODE_TRANS_ERR(code))
			ec_set_flag(ec, EC_FLAG_CAN_RETRY);
	}

cleanup:
	FREE(cmd);
	FREE(resp);
	return ec;
}

static errcode_t
ftp_quote(pd_t * pd, char * cmd, char ** resp)
{
	int               code  = 0;
	fh_t            * fh    = (fh_t *) pd->ftppriv;
	char            * next  = NULL;
	char            * cptr  = NULL;
	char            * token = NULL;
	errcode_t         ec    = EC_SUCCESS;

	*resp = NULL;

	/* Reconnect */
	ec = _f_reconnect(fh);
	if (ec)
		return ec;

	/* Mimic some responses. */
	cptr = Strdup(cmd);
	if ((token = StrtokEsc(cptr, ' ', &next)))
	{
		/* Wait */
		if (strcasecmp(token, "wait") == 0)
		{
			if (!StrtokEsc(next, ' ', &next))
			{
				s_setwait();
				*resp = Sprintf(NULL, 
				                "Wait has been %s.\r\n",
				                s_wait() ? "enabled" : "disabled");
				goto cleanup;
			}
			goto send_cmd;
		}

		/* Site Commands. */
		if (strcasecmp(token, "site") == 0)
		{
			if ((token = StrtokEsc(next, ' ', &next)))
			{
				/* Wait */
				if (strcasecmp(token, "wait") == 0)
				{
					if (!StrtokEsc(next, ' ', &next))
					{
						s_setwait();
						*resp = Sprintf(NULL, 
						                "Wait has been %s.\r\n",
				   		             s_wait() ? "enabled" : "disabled");
						goto cleanup;
					}
					goto send_cmd;
				}
			}
		}
	}

send_cmd:
	ec = _f_send_cmd(fh, cmd);
	if (ec != EC_SUCCESS)
		goto cleanup;

	ec = _f_get_final_resp(fh, &code, resp);
	if (ec != EC_SUCCESS)
		goto cleanup;

	/* Some responses require our attention. */
	FREE(cptr);
	next = NULL;
	cptr = Strdup(cmd);
	if ((token = StrtokEsc(cptr, ' ', &next)))
	{
		if (strcasecmp(token, "setfam") == 0)
		{
			if (strlen(next) > 0)
			{
				if (F_CODE_SUCC(code))
				{
					s_setfamily(next);
					FREE(*resp);
					*resp = Sprintf(NULL, "Family set to %s.\r\n", s_family());
				}
			} else
			{
				if (s_family() && F_CODE_SUCC(code))
				{
					FREE(*resp);
					*resp = Sprintf(NULL, "Family set to %s\n", s_family());
				}
			}
			goto cleanup;
		}
	
		/* Site Commands. */
		if (strcasecmp(token, "site") == 0)
		{
			if ((token = StrtokEsc(next, ' ', &next)))
			{
				if (strcasecmp(token, "setfam") == 0)
				{
					if (strlen(next) > 0)
					{
						if (F_CODE_SUCC(code))
						{
							s_setfamily(next);
							FREE(*resp);
							*resp = Sprintf(NULL, 
							                "Family set to %s.\r\n", 
							                s_family());
						}
					} else
					{
						if (s_family() && F_CODE_SUCC(code))
						{
							FREE(*resp);
							*resp = Sprintf(NULL, 
							                "Family set to %s\n", 
							                s_family());
						}
					}
					goto cleanup;
				}
			}
		}
	}

cleanup:
	Free(cptr);
	return ec;
}

static void
ftp_mlsx_feats(pd_t * pd, mf_t * mf)
{
	fh_t * fh = (fh_t *) pd->ftppriv;

	*mf = fh->mf;
}


static errcode_t
ftp_stat(pd_t * pd, char * path, ml_t ** mlp)
{
	int               code = 0;
	char            * rec  = NULL;
	char            * eor  = NULL;
	char            * cmd  = NULL;
	char            * resp = NULL;
	fh_t            * fh   = (fh_t *) pd->ftppriv;
	ml_t            * ml   = NULL;
	errcode_t         ec   = EC_SUCCESS;

	*mlp = NULL;

	/* Reconnect */
	ec = _f_reconnect(fh);
	if (ec)
		return ec;

	cmd = Sprintf(cmd, "MLST%s%s", path ? " " : "", path ? path : "");
	ec = _f_send_cmd(fh, cmd);
	if (ec)
		goto cleanup;

	ec = _f_get_final_resp(fh, &code, &resp);
	if (ec)
		goto cleanup;

	if (F_CODE_INTR(code))
		ec = ec_create(EC_GSI_SUCCESS,
		               EC_GSI_SUCCESS,
		               "Unexpected response to mlst: %s",
		               resp);

	if (F_CODE_ERR(code))
	{
		/* Attempt to mask 'No such file or directory' */
		if (strstr(resp, "No such file or directory"))
		{
			ec_destroy(ec);
			ec = EC_SUCCESS;
			goto cleanup;
		}

		ec = ec_create(EC_GSI_SUCCESS,
		               EC_GSI_SUCCESS,
		               "%s: %s",
		               path,
		               resp);
		if (F_CODE_TRANS_ERR(code))
			ec_set_flag(ec, EC_FLAG_CAN_RETRY);
	}

	if (ec)
		goto cleanup;

	rec = strstr(resp, "\r\n ");
	if (rec)
		eor = strstr(rec + 3, "\r\n");

	if (eor)
		*eor = '\0';

	if (rec && eor)
	{
		ml = *mlp = (ml_t *) malloc(sizeof(ml_t));
		_f_mlsx(rec+3, ml);
		ml->name = Strdup(path);
	}

	if (!rec || !eor)
		ec = ec_create(EC_GSI_SUCCESS,
		               EC_GSI_SUCCESS,
		               "%s: Bad server response:\n %s",
		               path,
		               resp);

cleanup:
	FREE(cmd);
	FREE(resp);

	return ec;
}

/*
 * token is provided to this layer to increase performance. Reading
 * large FTP directories is expensive. Knowing the token allows us
 * to optimize.
 */

static errcode_t
ftp_readdir(pd_t * pd, char * path, ml_t *** mlp, char * token)
{
	fh_t * fh      = (fh_t *) pd->ftppriv;
	errcode_t ec   = EC_SUCCESS;

	*mlp = NULL;

	/* Reconnect */
	ec = _f_reconnect(fh);
	if (ec)
		return ec;

	if (token == NULL || strcmp(token, "*") == 0)
		return _f_readdir_mlsd(pd, path, mlp, token);
	return _f_readdir_nlst(pd, path, mlp, token);

	return ec;
}

static errcode_t 
ftp_size(pd_t * pd, char * path, globus_off_t * size)
{
	ml_t    * mlp  = NULL;
	fh_t    * fh   = (fh_t *) pd->ftppriv;
	int       code = 0;
	char    * cmd  = NULL;
	char    * resp = NULL;
	errcode_t ec   = EC_SUCCESS;

	/* Reconnect */
	ec = _f_reconnect(fh);
	if (ec)
		return ec;

	if (fh->hasSize)
	{
		cmd = Sprintf(cmd, "SIZE %s", path);
		ec = _f_send_cmd(fh, cmd);
		FREE(cmd);
		cmd = NULL;

		if (ec != EC_SUCCESS)
			return ec;

		ec = _f_get_final_resp(fh, &code, &resp);
		if (ec != EC_SUCCESS)
			return ec;

		switch (code)
		{
		case 202:
		case 500:
		case 501:
		case 502:
			FREE(resp);
			fh->hasSize = 0;
			goto mlsx;
		}

		if (F_CODE_TRANS_ERR(code))
		{
			ec = ec_create(EC_GSI_SUCCESS,
			               EC_GSI_SUCCESS,
			               "%s",
			               resp);
			ec_set_flag(ec, EC_FLAG_CAN_RETRY);
			FREE(resp);
			return ec;
		}

		if (F_CODE_ERR(code))
		{
			FREE(resp);
			goto mlsx;
		}

		sscanf(resp, "%*d %"GLOBUS_OFF_T_FORMAT, size);
		FREE(resp);
		return ec;
	}

mlsx:
	if (fh->hasMlst && fh->mf.Size)
	{
		ec = ftp_stat(pd, path, &mlp);
		if (ec)
			return ec;

		if (mlp)
			*size = mlp->size;
		ml_delete(mlp);
		return ec;
	}

	return ec_create(EC_GSI_SUCCESS,
	                 EC_GSI_SUCCESS,
	                 "%s: Unable to determine size.",
	                 path);
}

static errcode_t
ftp_expand_tilde(pd_t * pd, char * tilde, char ** fullpath)
{
	*fullpath = Strdup(tilde);
	return EC_SUCCESS;
}

static errcode_t
ftp_stage(pd_t * pd, char * file, int * staged)
{
	errcode_t ec   = EC_SUCCESS;
	fh_t    * fh   = (fh_t *) pd->ftppriv;
	char    * cmd  = NULL;
	char    * resp = NULL;
	int       code = 0;

	*staged = 0;

	/* Reconnect */
	ec = _f_reconnect(fh);
	if (ec)
		return ec;

	cmd = Sprintf(NULL, "STAGE 0 %s", file);
	if (fh->hasStage)
		ec = _f_send_cmd(fh, cmd);
	FREE(cmd);

	if(ec)
		return ec;

	ec = _f_get_final_resp(fh, &code, &resp);
	if (ec)
		return ec;

	if (F_CODE_SUCC(code) || !code)
		*staged = 1;

	if (F_CODE_FINAL_ERR(code))
	{
		if (F_CODE_UNKNOWN(code))
		{
			fh->hasStage = 0;
			*staged = 1;
		} else /* (!F_CODE_UNKNOWN(code)) */
			ec = ec_create(EC_GSI_SUCCESS,
			               EC_GSI_SUCCESS,
			               "%s: %s",
			               file,
			               resp);
	}
	FREE(resp);
	return ec;
}

errcode_t
ftp_cksum (pd_t * pd, char * file, int * supported, unsigned int * crc)
{
	errcode_t ec   = EC_SUCCESS;
	fh_t    * fh   = (fh_t *) pd->ftppriv;
	char    * cmd  = NULL;
	char    * resp = NULL;
	char    * cptr = NULL;
	int       code = 0;
	int       ret  = 0;

	if (!fh->hasCksum)
	{
		*supported = 0;
		return ec;
	}

	/* Reconnect */
	ec = _f_reconnect(fh);
	if (ec)
		return ec;

	cmd = Sprintf(NULL, "site sum %s", file);
	ec = _f_send_cmd(fh, cmd);
	FREE(cmd);
	if(ec)
		return ec;

	ec = _f_get_final_resp(fh, &code, &resp);
	if (ec)
		return ec;

	*supported = 1;
	if (F_CODE_SUCC(code))
	{
		cptr = strstr(resp, "checksum =");
		if (cptr)
			ret = sscanf(cptr, "checksum = %u", crc);
		else
			ret = sscanf(resp + 3, "%u", crc);

		if (!ret)
			ec = ec_create(EC_GSI_SUCCESS,
			               EC_GSI_SUCCESS,
			               "Malformed SUM response: %s",
			               resp);
	} else
	{
		if (F_CODE_UNKNOWN(code))
		{
			fh->hasCksum = 0;
			*supported   = 0;
		} else /* (!F_CODE_UNKNOWN(code)) */
		{
			ec = ec_create(EC_GSI_SUCCESS,
			               EC_GSI_SUCCESS,
			               "%s: %s",
			               file,
			               resp);
			if (F_CODE_TRANS_ERR(code))
				ec_set_flag(ec, EC_FLAG_CAN_RETRY);
		}
	}
	FREE(resp);
	return ec;
}

#ifdef SYSLOG_PERF
char *
ftp_rhost(pd_t * pd)
{
	fh_t * fh   = (fh_t *) pd->ftppriv;
	return Strdup(fh->host);
}
#endif /* SYSLOG_PERF */

const Linterface_t FtpInterface = {
	ftp_connect,
	ftp_connected,
	ftp_disconnect,
	ftp_retr,
	ftp_stor,
	NULL, /* appefile */
	ftp_read,
	ftp_write,
	ftp_close,
	ftp_list,
	ftp_pwd,
	ftp_chdir,
	ftp_chgrp,
	ftp_chmod,
	ftp_mkdir,
	ftp_rename,
	ftp_rm,
	ftp_rmdir,
	ftp_quote,
	ftp_mlsx_feats,
	ftp_stat,
	ftp_readdir,
	ftp_size,
	ftp_expand_tilde,
	ftp_stage,
	ftp_cksum,
#ifdef SYSLOG_PERF
	ftp_rhost,
#endif /* SYSLOG_PERF */
};


static errcode_t
_f_send_cmd(fh_t * fh, char * cmd)
{
	errcode_t ec = EC_SUCCESS;
	char * buf   = Sprintf(NULL, "%s\r\n", cmd);
	char * wmsg  = NULL;

	o_printf(DEBUG_VERBOSE, buf);

	if (strncasecmp(buf, "ADAT ", 5) == 0)
	{
		wmsg = Strdup(buf);
	}
	else
	{
		ec = gsi_cc_wrap(fh->cc.gh, buf, &wmsg);
		if (ec != EC_SUCCESS)
			return ec;

		if (strcmp(wmsg, buf))
		{
			wmsg = Strcat(wmsg, "\r\n");
			o_printf(DEBUG_NOISY, "Encoded cmd: %s", wmsg);
		}
	}

	ec = net_write(fh->cc.nh, wmsg, strlen(wmsg));

	if (!ec)
		fh->rsp++;

	if (ec)
		ec_set_flag(ec, EC_FLAG_SHOULD_RETRY);

	FREE(buf);
	FREE(wmsg);
	return ec;
}

static errcode_t
_f_prep_dc(fh_t * fh, 
           fh_t * ofh, 
           int    stream,
           int    binary,
           int    retr)
{
	errcode_t ec    = EC_SUCCESS;

	ec = _f_setup_tcp(fh, ofh);
	if (ec)
		return ec;

	ec = _f_setup_mode(fh, ofh, stream);
	if (ec)
		return ec;

	ec = _f_setup_dcau(fh, ofh);
	if (ec)
		return ec;

	ec = _f_setup_pbsz(fh, ofh);
	if (ec)
		return ec;

	ec = _f_setup_prot(fh, ofh);
	if (ec)
		return ec;

	ec = _f_setup_type(fh, ofh, binary);
	if (ec)
		return ec;

	ec = _f_setup_dci(fh, ofh);
	if (ec)
		return ec;

	ec = _f_setup_conntype(fh, ofh, retr);
	if (ec)
		return ec;
	return ec;
}

/*
 * Pops error responses, not successful responses.
 */
static errcode_t
_f_poll_resp(fh_t * fh, int * code, char ** resp)
{
	errcode_t ec = EC_SUCCESS;
	int read  = 0;

	*code = 0;
	*resp = NULL;

	ec = _f_peak_resp(fh, code);
	if (ec != EC_SUCCESS)
		return ec;

	if (F_CODE_SUCC(*code))
		return EC_SUCCESS;

	if (*code == 0)
	{
		ec = net_poll(fh->cc.nh, &read, NULL, 0);
		if(ec != EC_SUCCESS)
			return ec;

		if (!read)
			return ec;
	}

	ec = _f_get_resp(fh, code, resp);
	if (ec != EC_SUCCESS)
		return ec;

	if (F_CODE_SUCC(*code))
	{
		ec = _f_push_resp(fh, *resp);
		FREE(*resp);
		*resp = NULL;
	}

	return ec;
}

static errcode_t
_f_push_resp(fh_t * fh, char * resp)
{
	if (fh->cc.len < (strlen(resp)+fh->cc.cnt))
	{
		fh->cc.len += strlen(resp);
		fh->cc.buf = (char *) realloc(fh->cc.buf, fh->cc.len);
	}

	memmove(fh->cc.buf+strlen(resp), fh->cc.buf, fh->cc.cnt);
	memcpy(fh->cc.buf, resp, strlen(resp));
	fh->cc.cnt += strlen(resp);

	if (F_CODE_COMP(atoi(resp)))
		fh->rsp++;

	return EC_SUCCESS;
}

static errcode_t
_f_peak_resp(fh_t * fh, int * code)
{
	char * cptr = NULL;

	*code = 0;
	if (fh->cc.cnt >= 3)
	{
		cptr = Strndup(fh->cc.buf, 3);
		*code = atoi(cptr);
	}
	FREE(cptr);
	return EC_SUCCESS;
}

static errcode_t
_f_get_final_resp(fh_t * fh, int * code, char ** resp)
{
	errcode_t ec = EC_SUCCESS;

	*code = 0;
	*resp = NULL;

	if (!fh->rsp)
		return EC_SUCCESS;
		
	while (F_CODE_CONT(*code) && ec == EC_SUCCESS)
	{
		ec = _f_get_resp(fh, code, resp);
		if (F_CODE_CONT(*code))
			FREE(*resp);
	}
	return ec;
}

static errcode_t
_f_get_resp(fh_t * fh, int * code, char ** resp)
{
	errcode_t ec = EC_SUCCESS;
	char * cptr  = NULL;
	char * umsg  = NULL;
	size_t count = 0;
	char * eor   = NULL;
	int    lr    = 0;
	int    rd    = 0;
	char * nl    = 0;

	*resp = NULL;
	*code = 0;

	if (!fh->rsp)
		return ec;

	while (1)
	{
		if (fh->cc.cnt >= 3)
		{
			if (fh->cc.buf[3] == '-')
				lr = 1;

			cptr = fh->cc.buf;
			do {
				nl = Strnstr(cptr, "\r\n", fh->cc.cnt-(cptr-fh->cc.buf));
				if (nl)
				{
					if (lr && !strncmp(fh->cc.buf, cptr, 3) && cptr[3] == ' ')
						lr = 0;
					cptr = nl + 2;
				}
			} while (nl && lr);

			if (nl)
				break;
		}

		if (fh->cc.eof)
			break;

		if ((fh->cc.cnt + s_blocksize()) > fh->cc.len)
		{
			fh->cc.len += s_blocksize();
			fh->cc.buf = (char *) realloc(fh->cc.buf, fh->cc.len);
		}

		count = fh->cc.len - fh->cc.cnt;

		/*
		 * This will slow the loop. Otherwise it will use too much CPU
		 * on reads with no input ready (IE 'quote stage 60')
		 */
		ec = net_poll(fh->cc.nh, &rd, NULL, -1);
		if (ec)
			return ec;

		ec = net_read(fh->cc.nh, 
		              fh->cc.buf + fh->cc.cnt, 
		              &count, 
		              &fh->cc.eof);

		if (ec != EC_SUCCESS)
			return ec;

		fh->cc.cnt += count;
	}

	if (nl)
	{
		*code = atoi(fh->cc.buf);
		if (*code > 600)
		{
			o_printf(DEBUG_NOISY, "Encoded resp: ");
			o_fwrite(stdout, 
			         DEBUG_NOISY, 
			         fh->cc.buf,
			         nl - fh->cc.buf + 2);

			for (eor = nl+2, cptr = fh->cc.buf, nl = NULL; eor!=cptr; cptr=nl+2)
			{
				nl = Strnstr(cptr, "\r\n", eor-cptr);
				ec = gsi_cc_unwrap(fh->cc.gh, cptr+4, &umsg, nl-cptr-4);
				if (ec != EC_SUCCESS)
					break;

				*resp = Strcat(*resp, umsg);
				FREE(umsg);
			}

			if (ec == EC_SUCCESS)
				*code = atoi(*resp);

			fh->cc.cnt -= eor - fh->cc.buf;
			memmove(fh->cc.buf, eor, fh->cc.cnt);
		} else
		{
			*resp = Strndup(fh->cc.buf, nl - fh->cc.buf + 2);
			fh->cc.cnt -= nl + 2 - fh->cc.buf;
			memmove(fh->cc.buf, nl + 2, fh->cc.cnt);
		}

		o_printf(DEBUG_VERBOSE, "%s", *resp);

		if (F_CODE_COMP(*code))
			fh->rsp--;
	}

	if (!nl && fh->cc.cnt > 0)
	{
		cptr = Strndup(fh->cc.buf, fh->cc.cnt);
		ec = ec_create(EC_GSI_SUCCESS,
		               EC_GSI_SUCCESS,
		               "Bad response from server: %s",
		               cptr);
		FREE(cptr);
	}

	if (!ec && *code == 421)
	{
		ec = ec_create(EC_GSI_SUCCESS,
		               EC_GSI_SUCCESS,
		               "%s",
		               *resp);
		ec_set_flag(ec, EC_FLAG_SHOULD_RETRY);
		FREE(*resp)
	}

	/* This will trigger the reconnect mechanism. */
	if (ec)
		net_close(fh->cc.nh);

	return ec;
}

static errcode_t
_f_setup_tcp(fh_t * fh, fh_t * ofh)
{
	int         wsize = 0;
	int         code = 0;
	char      * resp  = NULL;
	char      * cmd   = NULL;
	errcode_t   ec    = EC_SUCCESS;

	wsize = s_tcpbuf();
	if (!wsize)
		return EC_SUCCESS;

	if (fh->hasWind)
	{
		cmd = Sprintf(NULL, "WIND %d", wsize);
		ec = _f_send_cmd(fh, cmd);
	} else if (fh->hasSbuf)
	{
		cmd = Sprintf(NULL, "SBUF %d", wsize);
		ec = _f_send_cmd(fh, cmd);
	} else if (fh->hasSiteBufsize)
	{
		cmd = Sprintf(NULL, "SITE BUFSIZE %d", wsize);
		ec = _f_send_cmd(fh, cmd);
	}
	FREE(cmd);

	if (ec)
		return ec;

	ec = _f_get_final_resp(fh, &code, &resp);
	if (ec)
		return ec;
	FREE(resp);

	if (F_CODE_UNKNOWN(code))
	{
		if (fh->hasWind)
			fh->hasWind = 0;
		else if (fh->hasSbuf)
			fh->hasSbuf = 0;
		else if (fh->hasSiteBufsize)
			fh->hasSiteBufsize = 0;

		return _f_setup_tcp(fh, ofh);
	}

	return EC_SUCCESS;
}

static errcode_t
_f_setup_mode(fh_t * fh, fh_t * ofh, int stream)
{
	int       code = 0;
	char    * resp = NULL;
	errcode_t ec   = EC_SUCCESS;

	fh->stream = (stream ||  /* We need stream */
	              s_stream() ||  /* We want stream */
	             !fh->hasParallel || /* The service doesnt support eb */
	             (ofh && !ofh->hasParallel)); /* The other service doesnt support eb */

	ec = _f_send_cmd(fh, fh->stream ? "MODE S" : "MODE E");

	if (ec != EC_SUCCESS)
		return ec;

	ec = _f_get_final_resp(fh, &code, &resp);
	if (!ec && code > 299)
			ec = ec_create(EC_GSI_SUCCESS,
			               EC_GSI_SUCCESS,
			               "%s",
			               resp);

	FREE(resp);
	return ec;
}

static errcode_t
_f_setup_dcau(fh_t * fh, fh_t * ofh)
{
	int         code = 0;
	char      * resp = NULL;
	char      * cmd  = NULL;
	errcode_t   ec   = EC_SUCCESS;

	if (!fh->hasDcau || (ofh && !ofh->hasDcau))
		fh->dcs.dcau = 0;
	else
		fh->dcs.dcau = s_dcau() != 0;

	if (!fh->hasDcau)
		return ec;

	if (!fh->dcs.dcau)
	{
		ec = _f_send_cmd(fh, "DCAU N");
	} else if (ofh && s_dcau() == 2)
	{
		cmd = Sprintf(NULL, "DCAU S %s", s_dcau_subject());
		ec = _f_send_cmd(fh, "DCAU S %s");
		FREE(cmd);
	} else
	{
		ec = _f_send_cmd(fh, "DCAU A");
	}

	if (ec != EC_SUCCESS)
		return ec;

	ec = _f_get_final_resp(fh, &code, &resp);
	if (!ec && code > 299)
			ec = ec_create(EC_GSI_SUCCESS,
			               EC_GSI_SUCCESS,
			               "%s",
			               resp);

	FREE(resp);
	return ec;
}

static errcode_t
_f_setup_pbsz(fh_t * fh, fh_t * ofh)
{
	int         code = 0;
	int         pbsz = 0;
	char      * cmd  = NULL;
	char      * pstr = NULL;
	char      * resp = NULL;
	errcode_t   ec   = EC_SUCCESS;

	fh->dcs.pbsz = 0;
	if (!fh->dcs.dcau)
		return ec;

	pbsz = s_pbsz();
	if (pbsz == 0)
	{
		ec = gsi_pbsz_maxpmsg(fh->cc.gh, s_blocksize(), &pbsz);
		if (ec)
			return ec;
	}

	cmd = Sprintf(NULL, "PBSZ %d", pbsz);
	ec  = _f_send_cmd(fh, cmd);
	FREE(cmd);

	if (ec)
		return ec;

	ec = _f_get_final_resp(fh, &code, &resp);
	if (ec)
		return ec;

	if (code > 399)
		ec = ec_create(EC_GSI_SUCCESS,
		               EC_GSI_SUCCESS,
		               "%s",
		               resp);

	if (F_CODE_SUCC(code));
	{
		pstr = Strcasestr(resp, "PBSZ=");
		if (pstr)
			pbsz = atoi(pstr+5);
	}
	fh->dcs.pbsz = pbsz;

	FREE(resp);
	return ec;
}


static errcode_t
_f_setup_prot(fh_t * fh, fh_t * ofh)
{
	int         code = 0;
	char      * resp = NULL;
	errcode_t   ec   = EC_SUCCESS;

	if (!fh->dcs.dcau)
		return ec;

	switch (s_prot())
	{
	case 0: /* Clear */
		ec = _f_send_cmd(fh, "PROT C");
		break;
	case 1: /* Safe  */
		ec = _f_send_cmd(fh, "PROT S");
		break;
	case 2: /* Confidential */
		ec = _f_send_cmd(fh, "PROT E");
		break;
	case 3: /* Private */
		ec = _f_send_cmd(fh, "PROT P");
		break;
	}

	if (ec)
		return ec;

	ec = _f_get_final_resp(fh, &code, &resp);
	if (ec)
		return ec;

	if (code > 399)
		ec = ec_create(EC_GSI_SUCCESS,
		               EC_GSI_SUCCESS,
		               "%s",
		               resp);

	FREE(resp);
	return ec;
}

static errcode_t
_f_setup_type(fh_t * fh, fh_t * ofh, int binary)
{
	int         code = 0;
	char      * resp = NULL;
	errcode_t   ec   = EC_SUCCESS;

	fh->ascii = 1;
	if (binary || !fh->stream || !s_ascii())
		fh->ascii = 0;

	ec = _f_send_cmd(fh, fh->ascii ? "TYPE A" : "TYPE I");
	if (ec != EC_SUCCESS)
		return ec;

	ec = _f_get_final_resp(fh, &code, &resp);
	if (!ec && code > 299)
			ec = ec_create(EC_GSI_SUCCESS,
			               EC_GSI_SUCCESS,
			               "%s",
			               resp);

	FREE(resp);
	return ec;
}

static errcode_t
_f_setup_dci(fh_t * fh, fh_t * ofh)
{
	if (ofh)
		return EC_SUCCESS;
	if (fh->ascii)
		fh->dcs.dci = Ftp_a_dci;
	else if (fh->stream)
		fh->dcs.dci = Ftp_s_dci;
	else
		fh->dcs.dci = Ftp_eb_dci;
	return EC_SUCCESS;
}

static errcode_t
_f_setup_spor(fh_t * fh, fh_t * ofh, int retr)
{
	errcode_t ec   = EC_SUCCESS;
	int       i    = 0;
	int       code = 0;
	int       scnt = 1;
	char    * cmd  = NULL;
	char    * resp = NULL;
	struct sockaddr_in   sin;
	struct sockaddr_in * sinp = NULL;

	if (ofh)
	{
		sinp = ofh->sinp;
		scnt = ofh->scnt;
	}

	if (!sinp)
	{
		ec = net_getsockname(fh->cc.nh, &sin);
		if (ec)
			return ec;
		sin.sin_port = 0;

		ec = fh->dcs.dci.passive(&fh->dcs, &sin);
		if (ec)
			return ec;
		sinp = &sin;
	}

	for (i = 0; i < scnt; i++)
	{
		cmd = Sprintf(cmd, 
		             "SPOR %d,%d,%d,%d,%d,%d",
		             (ntohl(sinp[i].sin_addr.s_addr) >> 24) & 0xFF,
		             (ntohl(sinp[i].sin_addr.s_addr) >> 16) & 0xFF,
		             (ntohl(sinp[i].sin_addr.s_addr) >>  8) & 0xFF,
		             (ntohl(sinp[i].sin_addr.s_addr) >>  0) & 0xFF,
		             (ntohs(sinp[i].sin_port) >> 8) & 0xFF,
		             (ntohs(sinp[i].sin_port) >> 0) & 0xFF);
	}

	ec = _f_send_cmd(fh, cmd);
	FREE(cmd);
	if (ec)
		return ec;

	ec = _f_get_final_resp(fh, &code, &resp);
	if (ec)
		return ec;

	if (!F_CODE_SUCC(code))
		ec = ec_create(EC_GSI_SUCCESS, EC_GSI_SUCCESS, resp);
	FREE(resp);
	return ec;
}

static errcode_t
_f_setup_spas(fh_t * fh, fh_t * ofh, int retr)
{
	errcode_t ec   = EC_SUCCESS;
	int       code = 0;
	int       scnt = 0;
	char    * resp = NULL;
	struct sockaddr_in * sinp = NULL;

	ec = _f_send_cmd(fh, "SPAS");
	if (ec)
		return ec;

	ec = _f_get_final_resp(fh, &code, &resp);
	if (ec)
		return ec;

	if (F_CODE_INTR(code))
	{
		ec = ec_create(EC_GSI_SUCCESS,
		               EC_GSI_SUCCESS,
		               "Bad response to SPAS: %s",
		               resp);
		FREE(resp);
		return ec;
	}

	if (F_CODE_ERR(code))
	{
		ec = ec_create(EC_GSI_SUCCESS,
		               EC_GSI_SUCCESS,
			           "SPAS command failed: %s",
		               resp);
		FREE(resp);
		return ec;
	}

	scnt = _f_parse_addrs(resp, &sinp);

	if (scnt == 0)
	{
		ec = ec_create(EC_GSI_SUCCESS,
		               EC_GSI_SUCCESS,
		               "No addresses found in SPAS response:\n%s",
		               resp);
		FREE(resp);
		return ec;
	}
	FREE(resp);

	if (ofh)
	{
		/* Third party */
		fh->scnt = scnt;
		fh->sinp = sinp;
		return ec;
	}

	ec = fh->dcs.dci.active(&fh->dcs, sinp, scnt);
	FREE(sinp);
	return ec;
}

static errcode_t
_f_setup_port(fh_t * fh, fh_t * ofh, int retr)
{
	errcode_t ec   = EC_SUCCESS;
	int       code = 0;
	char    * cmd  = NULL;
	char    * resp = NULL;
	struct sockaddr_in   sin;

	if (ofh)
		memcpy(&sin, ofh->sinp, sizeof(struct sockaddr_in));

	if (!ofh)
	{
		ec = net_getsockname(fh->cc.nh, &sin);
		if (ec)
			return ec;
		sin.sin_port = 0;

		ec = fh->dcs.dci.passive(&fh->dcs, &sin);
		if (ec)
			return ec;
	}

	cmd = Sprintf(cmd,
	        "PORT %d,%d,%d,%d,%d,%d",
	        (ntohl(sin.sin_addr.s_addr) >> 24) & 0xFF,
	        (ntohl(sin.sin_addr.s_addr) >> 16) & 0xFF,
	        (ntohl(sin.sin_addr.s_addr) >>  8) & 0xFF,
	        (ntohl(sin.sin_addr.s_addr) >>  0) & 0xFF,
	        (ntohs(sin.sin_port) >> 8) & 0xFF,
	        (ntohs(sin.sin_port) >> 0) & 0xFF);

	ec = _f_send_cmd(fh, cmd);
	FREE(cmd);
	if (ec)
		return ec;

	ec = _f_get_final_resp(fh, &code, &resp);
	if (ec)
		return ec;

	if (!F_CODE_SUCC(code))
		ec = ec_create(EC_GSI_SUCCESS, EC_GSI_SUCCESS, resp);
	FREE(resp);
	return ec;
}

static errcode_t
_f_setup_pasv(fh_t * fh, fh_t * ofh, int retr)
{
	errcode_t ec   = EC_SUCCESS;
	int       code = 0;
	int       scnt = 0;
	char    * resp = NULL;
	struct sockaddr_in * sinp = NULL;

	/* Passive retry */
	if (ofh && fh->sinp)
		return ec;

	if (ofh && !ofh->hasPasv && !fh->hasPasv)
		return ec_create(
	                 EC_GSI_SUCCESS,
	                 EC_GSI_SUCCESS,
	                 "No compatible transfer mode between services.\n%s",
	                 "Neither service supports passive.");

	if (ofh && !fh->hasPasv)
	{
		ofh->stream = 1;
		ec = _f_setup_conntype(ofh, fh, retr);
		if (ec)
			return ec;
		return _f_setup_conntype(fh, ofh, retr);
	}

	if (!fh->hasPasv)
		return ec_create(
		      EC_GSI_SUCCESS,
		      EC_GSI_SUCCESS,
		      "This service does not support passive connections.");

	ec = _f_send_cmd(fh, "PASV");
	if (ec)
		return ec;

	ec = _f_get_final_resp(fh, &code, &resp);
	if (ec)
		return ec;

	if (F_CODE_INTR(code))
	{
		ec = ec_create(EC_GSI_SUCCESS,
		               EC_GSI_SUCCESS,
		               "Bad response to PASV: %s",
		               resp);
		FREE(resp);
		return ec;
	}

	if (F_CODE_ERR(code))
	{
		/* Passive retry. */
		fh->hasPasv = 0;
		FREE(resp);
		if (ofh)
		{
			ofh->stream = 1;
			ec = _f_setup_conntype(ofh, fh, retr);
			if (ec)
				return ec;
		}
		return _f_setup_conntype(fh,  ofh, retr);
	}

	scnt = _f_parse_addrs(resp, &sinp);

	if (scnt == 0)
	{
		ec = ec_create(EC_GSI_SUCCESS,
		               EC_GSI_SUCCESS,
		               "No addresses found in PASV response:\n%s",
		               resp);
		FREE(resp);
		return ec;
	}
	FREE(resp);

	if (ofh)
	{
		/* Third party */
		fh->scnt = scnt;
		fh->sinp = sinp;
		return ec;
	}

	ec = fh->dcs.dci.active(&fh->dcs, sinp, scnt);
	FREE(sinp);
	return ec;
}

static errcode_t
_f_setup_conntype(fh_t * fh, fh_t * ofh, int retr)
{
	errcode_t ec = EC_SUCCESS;

	/* Setup the passive side. */
	switch (fh->stream)
	{
	case 0: /* Extended block */
		switch (retr)
		{
		case 0:
			return _f_setup_spas(fh, ofh, retr);
		default:
			return _f_setup_spor(fh, ofh, retr);
		}

	default: /* Streams */
		switch((!ofh && s_passive()) || (ofh && !ofh->sinp))
		{
		case 0: /* Port */
			return _f_setup_port(fh, ofh, retr);

		default: /* Pasv */
			return _f_setup_pasv(fh, ofh, retr);
		}
	}

	return ec;
}

static int
_f_parse_addrs(
    char                 *  str,
	struct sockaddr_in  **  sinp)
{
    char * ptr = NULL;
    int x1, x2, x3, x4, x5, x6;
	int cnt = 0;
    int c;

    *sinp = NULL;
    ptr = str;
    while ((ptr = strchr(ptr, ',')) != NULL)
    {
        while (ptr != str && isdigit(*(ptr-1)))
            ptr--;

        c = sscanf(ptr, "%d,%d,%d,%d,%d,%d", &x1, &x2, &x3, &x4, &x5, &x6);

        if (c == 6)
        {
            /* Match */
            *sinp = (struct sockaddr_in *) realloc(
			              *sinp,
                          (++cnt)*sizeof(struct sockaddr_in));

			(*sinp)[cnt-1].sin_family = AF_INET;
			(*sinp)[cnt-1].sin_addr.s_addr = htonl(((x1 << 24) & 0xFF000000) |
			                                       ((x2 << 16) & 0x00FF0000) |
			                                       ((x3 <<  8) & 0x0000FF00) |
			                                       ((x4 <<  0) & 0x000000FF));
			(*sinp)[cnt-1].sin_port = htons(((x5 << 8) & 0xFF00) | 
			                                ((x6 << 0) & 0x00FF));

            ptr = strchr(ptr, '\n');
            if (ptr == NULL)
                break;
        } else
        {
            ptr = strchr(ptr, ',');
            ptr++;
        }
    }
	return cnt;
}

static void
_f_mlsx(char * str, ml_t * ml)
{
	char * token = NULL;
	char * cptr = Strdup(str);
	char * sptr = cptr;
	char * lasts = NULL;

	memset(ml, 0, sizeof(ml_t));

	while((token = strtok_r(cptr, ";", &lasts)) != NULL)
	{
		cptr = NULL;
		if (strncasecmp(token, "type=", 5) == 0)
		{
			if (strcasecmp(token+5, "file") == 0)
			{
				ml->type = S_IFREG;
			} else if (strcasecmp(token+5, "cdir") == 0)
			{
				ml->type = S_IFDIR;
			} else if (strcasecmp(token+5, "pdir") == 0)
			{
				ml->type = S_IFDIR;
			} else if (strcasecmp(token+5, "dir") == 0)
			{
				ml->type = S_IFDIR;
			} else if (strcasecmp(token+5, "OS.unix=slink") == 0)
			{
				ml->type = S_IFLNK;
			} else
			{
				ml->type = S_IFCHR;
			}
			ml->mf.Type = 1;
		} else if (strncasecmp(token, "size=", 5) == 0)
		{
			sscanf(token+5, "%"GLOBUS_OFF_T_FORMAT, &ml->size);
			ml->mf.Size = 1;
		} else if (strncasecmp(token, "modify=", 7) == 0)
		{
			ml->modify = ModFactToTime(token+7);
			ml->mf.Modify = 1;
		} else if (strncasecmp(token, "perm=", 5) == 0)
		{
			for (token += 5; *token; token++)
			{
				switch (*token)
				{
				case 'a':
					ml->perms.appe = 1;
					break;
				case 'c':
					ml->perms.creat = 1;
					break;
				case 'd':
					ml->perms.delete = 1;
					break;
				case 'e':
					ml->perms.exec = 1;
					break;
				case 'f':
					ml->perms.rename = 1;
					break;
				case 'l':
					ml->perms.list = 1;
					break;
				case 'm':
					ml->perms.mkdir = 1;
					break;
				case 'p':
					ml->perms.purge = 1;
					break;
				case 'r':
					ml->perms.retrieve = 1;
					break;
				case 'w':
					ml->perms.store = 1;
					break;
				}
			}
			ml->mf.Perm = 1;
		} else if (strncasecmp(token, "charset=", 8) == 0)
		{
		} else if (strncasecmp(token, "UNIX.mode=", 10) == 0)
		{
			ml->UNIX_mode = Strdup(token+10);
			ml->mf.UNIX_mode = 1;
		} else if (strncasecmp(token, "UNIX.owner=", 11) == 0)
		{
			ml->UNIX_owner = Strdup(token+11);
			ml->mf.UNIX_owner = 1;
		} else if (strncasecmp(token, "UNIX.group=", 11) == 0)
		{
			ml->UNIX_group = Strdup(token+11);
			ml->mf.UNIX_group = 1;
		} else if (strncasecmp(token, "unique=", 7) == 0)
		{
			ml->unique = Strdup(token+7);
			ml->mf.Unique = 1;
		} else if (strncasecmp(token, "UNIX.slink=", 11) == 0)
		{
			ml->UNIX_slink = Strdup(token+11);
			ml->mf.UNIX_slink = 1;
		} else if (strncasecmp(token, "X.family=", 9) == 0)
		{
			ml->X_family = Strdup(token+9);
			ml->mf.X_family = 1;
		} else if (strncasecmp(token, "X.archive=", 10) == 0)
		{
			ml->X_archive = Strdup(token+10);
			ml->mf.X_archive = 1;
		}
	}

	FREE(sptr);
}

static errcode_t
_f_keepalive(fh_t * fh)
{
	errcode_t ec = EC_SUCCESS;

	if (s_keepalive() && (time(NULL) > (s_keepalive() + fh->keepalive)))
	{
		ec = _f_send_cmd(fh, "NOOP");
		if (ec)
			return ec;

		fh->keepalive = time(NULL);
	}

	return EC_SUCCESS;
}

static void
_f_destroy_pd(pd_t * pd)
{
	fh_t * fh = (fh_t *) pd->ftppriv;

	if (fh == NULL)
		return;

	gsi_destroy(fh->cc.gh);
	net_destroy(fh->cc.nh);

	FREE(fh->host);
	FREE(fh->rhost);
	FREE(fh->pass);
	FREE(fh->user);
	FREE(fh->ocwd);

	FREE(pd->ftppriv);
	pd->ftppriv = NULL;
	return;
}

static errcode_t
_f_connect(fh_t * fh, char ** srvrmsg)
{
	errcode_t   ec    = EC_SUCCESS;
	char      * resp  = NULL;
	char      * cptr  = NULL;
	char      * token = NULL;
	int         code  = 0;
	struct sockaddr_in sin;

	if (net_connected(fh->cc.nh))
		return ec;

	if (fh->cc.nh)
		o_printf(DEBUG_NORMAL, "Reconnecting...\n");

	if (fh->cc.gh)
		gsi_destroy(fh->cc.gh);
	fh->cc.gh = NULL;
	fh->rsp   = 1; /* MOTD */

	if (!fh->rhost)
		fh->rhost = GetRealHostName(fh->host);
	if (!fh->rhost)
		fh->rhost = Strdup(fh->host);

	ec = net_translate(fh->rhost, fh->port, &sin);
	if (ec != EC_SUCCESS)
		goto cleanup;

	ec = net_connect(&fh->cc.nh, &sin);
	if (ec != EC_SUCCESS)
		goto cleanup;

	/* Motd */
	ec = _f_get_final_resp(fh, &code, &resp);

	if (ec != EC_SUCCESS || F_CODE_ERR(code))
		goto cleanup;
	if (srvrmsg)
		*srvrmsg = Strdup(resp);
	FREE(resp);

	if (fh->pass)
	{
		cptr = Sprintf(NULL, "USER %s", fh->user);
		ec = _f_send_cmd(fh, cptr);
		FREE(cptr);
		if (ec != EC_SUCCESS)
			goto cleanup;

		ec = _f_get_final_resp(fh, &code, &resp);
		if (ec != EC_SUCCESS || F_CODE_ERR(code))
			goto cleanup;
		FREE(resp);

		cptr = Sprintf(NULL, "PASS %s", fh->pass);
		ec = _f_send_cmd(fh, cptr);
		FREE(cptr);
		if (ec != EC_SUCCESS)
			goto cleanup;

		ec = _f_get_final_resp(fh, &code, &resp);
		if (ec != EC_SUCCESS || F_CODE_ERR(code))
			goto cleanup;

		if (srvrmsg)
			*srvrmsg = Strcat(*srvrmsg, resp);
		FREE(resp);

	} else
	{
		ec = gsi_cc_init(&fh->cc.gh, fh->rhost);
		if (ec != EC_SUCCESS)
			goto cleanup;

		ec = _f_send_cmd(fh, "AUTH GSSAPI");
		if (ec != EC_SUCCESS)
			goto cleanup;

		ec = _f_get_final_resp(fh, &code, &resp);
		if (ec != EC_SUCCESS || F_CODE_ERR(code))
			goto cleanup;
		FREE(resp);
		resp = NULL;

		do
		{
			ec = gsi_init_sec_context(fh->cc.gh,
			                          resp ? resp + 9 : NULL,
			                          &token);
			FREE(resp);
			if (ec != EC_SUCCESS)
				goto cleanup;

			if (token)
				cptr = Sprintf(NULL, "ADAT %s", token);
			else
				cptr = Strdup("ADAT");

			FREE(token);

			ec = _f_send_cmd(fh, cptr);
			FREE(cptr);
			if (ec != EC_SUCCESS)
				goto cleanup;

			ec = _f_get_final_resp(fh, &code, &resp);
			if (ec != EC_SUCCESS || F_CODE_ERR(code))
				goto cleanup;

		} while (F_CODE_INTR(code) && ec == EC_SUCCESS);
		FREE(resp);

		if (F_CODE_SUCC(code))
		{
			if (!fh->user)
				fh->user = Strdup(":globus-mapping:");

			cptr = Sprintf(NULL, "USER %s", fh->user);

			ec = _f_send_cmd(fh, cptr);
			FREE(cptr);
			if (ec != EC_SUCCESS)
				goto cleanup;

			ec = _f_get_final_resp(fh, &code, &resp);
			if (ec != EC_SUCCESS || F_CODE_ERR(code))
				goto cleanup;

			if (F_CODE_INTR(code))
			{
				FREE(resp);
				ec = _f_send_cmd(fh, "PASS dummy");
				if (ec != EC_SUCCESS)
					goto cleanup;

				ec = _f_get_final_resp(fh, &code, &resp);
				if (ec != EC_SUCCESS || F_CODE_ERR(code))
					goto cleanup;
			}
			if (srvrmsg)
				*srvrmsg = Strcat(*srvrmsg, resp);
			FREE(resp);
		}
	}

	if (fh->cwd)
		ec = _f_chdir(fh, fh->cwd);

cleanup:
	if (ec == EC_SUCCESS && F_CODE_ERR(code))
	{
		ec = ec_create(EC_GSI_SUCCESS, EC_GSI_SUCCESS, "%s", resp);
		if (F_CODE_TRANS_ERR(code))
			ec_set_flag(ec, EC_FLAG_CAN_RETRY);
		FREE(resp);
	}

	if (ec)
		net_close(fh->cc.nh);

	return ec;
}

static errcode_t
_f_reconnect(fh_t * fh)
{
	return _f_connect(fh, NULL);
}

static errcode_t
_f_chdir(fh_t * fh, char * path)
{
	int               code = 0;
	char            * cmd  = NULL;
	char            * resp = NULL;
	errcode_t         ec   = EC_SUCCESS;

	cmd = Sprintf(cmd, "CWD%s%s", path ? " " : "", path ? path : "");
	ec = _f_send_cmd(fh, cmd);
	if (ec != EC_SUCCESS)
		goto cleanup;

	ec = _f_get_final_resp(fh, &code, &resp);
	if (ec != EC_SUCCESS)
		goto cleanup;

	if (F_CODE_INTR(code))
		ec = ec_create(EC_GSI_SUCCESS,
		               EC_GSI_SUCCESS,
		               "Unexpected response to chdir: %s",
		               resp);

	if (F_CODE_ERR(code))
	{
		ec = ec_create(EC_GSI_SUCCESS,
		               EC_GSI_SUCCESS,
		               "%s",
		               resp);
		if (F_CODE_TRANS_ERR(code))
			ec_set_flag(ec, EC_FLAG_CAN_RETRY);
	}

cleanup:
	FREE(cmd);
	FREE(resp);
	return ec;
}

/*
 * readdir commands should only return the basename of the entry.
 */
static errcode_t
_f_readdir_mlsd(pd_t * pd, char * path, ml_t *** mlp, char * token)
{
	fh_t * fh      = (fh_t *) pd->ftppriv;
	int    index   = 0;
	char * buf     = NULL;
	char * listing = NULL;
	char * rec     = NULL;
	char * eor     = NULL;
	char * name    = NULL;
	char * cmd     = NULL;
	errcode_t ec   = EC_SUCCESS;
	errcode_t ec2  = EC_SUCCESS;
	size_t        len = 0;
	int           eof = 0;
	globus_off_t  off = 0;

	*mlp = NULL;

	ec = _f_prep_dc(fh, NULL, 1, 1, 1);
	if (ec)
		goto cleanup;

	cmd = Sprintf(cmd, "MLSD%s%s", path ? " " : "", path ? path : "");
	ec = _f_send_cmd(fh, cmd);
	FREE(cmd);
	if (ec)
		goto cleanup;
	fh->keepalive = time(NULL);

	while (!eof)
	{
		ec = ftp_read(pd, &buf, &off, &len, &eof);
		if (ec)
			break;

		listing = Strncat(listing, buf, len);
		FREE(buf);
	}

	if (ec)
		goto cleanup;

	o_printf(DEBUG_VERBOSE, "MLSD output:\n");
	o_printf(DEBUG_VERBOSE, "%s", listing);

	for (rec = listing; ec == EC_SUCCESS && rec && *rec; rec = eor + 2)
	{
		eor = strstr(rec, "\r\n");
		if (eor)
			*eor = '\0';

		if (eor)
			name = strstr(rec, "; ");

		if (token && name)
		{
			/* Regular expression match. */
			if (s_glob() && fnmatch(token, name, 0))
				continue;

			/* Literal match. */
			if (!s_glob() && strcmp(token, name) != 0)
				continue;
		}

		if (eor && name)
		{
			*mlp = (ml_t**) realloc(*mlp, (index+2)*sizeof(ml_t*));
			(*mlp)[index]   = (ml_t *) malloc(sizeof(ml_t));
			(*mlp)[index+1] = NULL;

			_f_mlsx(rec, (*mlp)[index]);

			/* Allow '.' and '..' to the upper layer. */
			if (eor && Strcasestr(rec, "type=cdir"))
				(*mlp)[index]->name = Strdup(".");
			else if (eor && Strcasestr(rec, "type=pdir"))
				(*mlp)[index]->name = Strdup("..");
			else
				(*mlp)[index]->name = Strdup(name + 2);

			index++;
		}

		if (!eor || !name)
			ec = ec_create(EC_GSI_SUCCESS,
			               EC_GSI_SUCCESS,
			               "Bad server response:\n %s",
			               rec);
	}

cleanup:

	ec2 = ftp_close(pd);
	if (ec != EC_SUCCESS)
		ec_destroy(ec2);
	else
		ec = ec2;

	if (ec)
	{
		for (index = 0; *mlp && (*mlp)[index]; index++)
			ml_delete((*mlp)[index]);
		FREE(*mlp);
		*mlp = NULL;
	}

	FREE(listing);
	if (ec)
	{
		if (path)
			ec_prepend_msg(ec, "%s:", path);
		else
			ec_prepend_msg(ec, "Current working directory:");
	}
	return ec;
}

/*
 * readdir commands should only return the basename of the entry.
 */
static errcode_t
_f_readdir_nlst(pd_t * pd, char * path, ml_t *** mlpp, char * token)
{
	fh_t * fh      = (fh_t *) pd->ftppriv;
	ml_t * mlp     = NULL;
	int    index   = 0;
	char * name    = NULL;
	char * buf     = NULL;
	char * listing = NULL;
	char * cmd     = NULL;
	char * rec     = NULL;
	char * eor     = NULL;
	char * eol     = NULL;
	char * bname   = NULL;
	errcode_t ec   = EC_SUCCESS;
	errcode_t ec2  = EC_SUCCESS;
	size_t        len = 0;
	int           eof = 0;
	globus_off_t  off = 0;

	*mlpp = NULL;

	ec = _f_prep_dc(fh, NULL, 1, 1, 1);
	if (ec)
		goto cleanup;

	/*
	 * Non regexp NLST output comes back as:
	 *   dir:  prefix,
	 *   dir/entries or, 
	 *   entries
	 *
	 * On error we get:
	 *   /blah No such file or directory       (data channel)
	 *   550 /blah: No such file or directory. (control channel)
	 *   500-Command failed. : System error in stat: No such file or directory
	 *   500-A system call failed: No such file or directory
	 *   500 End.  (Control channel)
	 */

	/* NLIST first. */
	cmd = Sprintf(cmd, "NLST%s%s", path ? " " : "", path ? path : "");
	ec = _f_send_cmd(fh, cmd);
	FREE(cmd);
	if (ec)
		goto cleanup;
	fh->keepalive = time(NULL);

	while (!eof)
	{
		ec = ftp_read(pd, &buf, &off, &len, &eof);
		if (ec)
			break;

		listing = Strncat(listing, buf, len);
		FREE(buf);
	}

	ec2 = ftp_close(pd);
	if (ec != EC_SUCCESS)
		ec_destroy(ec2);
	else
		ec = ec2;

	/* Error here does not mean 'No Match' */
	if (ec)
		goto cleanup;

	o_printf(DEBUG_VERBOSE, "NLST output:\n");
	o_printf(DEBUG_VERBOSE, "%s", listing);

	if (strstr(listing, "No such file or directory"))
	{
		ec = ec_create(EC_GSI_SUCCESS,
		               EC_GSI_SUCCESS,
		               "%s%s%s%s",
		               path ? path : "",
		               path ? ":"  : "",
		               path ? " "  : "",
		               path ? "No such file or directory"  : "");
		goto cleanup;
	}

	if (strstr(listing, "Permission denied"))
	{
		ec = ec_create(EC_GSI_SUCCESS,
		               EC_GSI_SUCCESS,
		               "%s%s%s%s",
		               path ? path : "",
		               path ? ":"  : "",
		               path ? " "  : "",
		               path ? "Permission denied"  : "");
		goto cleanup;
	}

	/* Loop through and stat anything that matches. */
	eol = listing + strlen(listing) - 1;
	for (rec = listing; rec && rec < eol; rec = eor + 1)
	{
		/* Find the end of the record. */
		eor = strstr(rec, "\n");
		if (!eor)
			eor = rec + strlen(eor);

		/* Null terminate. */
		*eor = '\0';
		if (*(eor - 1) == '\r')
			*(eor - 1) = '\0';

		/* Make sure we have something useful. */
		if (strlen(rec) == 0)
			continue;

		/* Special checks for first entry. */
		if (rec == listing)
		{
			/* Breakout if no match was returned. */
			if (strstr(rec, "No such file or directory"))
				break;

			/* Ignore directory headers. */
			if (*(rec + strlen(rec) - 1) == ':')
				continue;
		}

		bname = Basename(rec);

		if (bname)
		{
			/* Regular expression match. */
			if (s_glob() && fnmatch(token, bname, 0))
				continue;

			/* Literal match. */
			if (!s_glob() && strcmp(token, bname) != 0)
				continue;
		}

		name  = Sprintf(NULL, 
		                "%s%s%s", 
		                path ? path : "", 
		                path ? "/" : "", 
		                bname);
		ec = ftp_stat(pd, name, &mlp);
		FREE(name);
		if (ec)
			break;

		/*
		 * If no mlp, ENOENT. This should not happen since NLST returned
		 * it but it does happen with bad entries on Unitree and it will
		 * happen with race conditions (someone else removed the file).
		 */
		if (mlp)
		{
			/* Change mlp->name to the base name. */
			Free(mlp->name);
			mlp->name = Strdup(bname);

			/* Move the mlp into the mlpp array. */
			*mlpp = (ml_t **) realloc(*mlpp, (index + 2) * sizeof(ml_t *));
			(*mlpp)[index++] = mlp;
			(*mlpp)[index] = NULL;
		}
	}

cleanup:

	if (ec)
	{
		for (index = 0; *mlpp && (*mlpp)[index]; index++)
			ml_delete((*mlpp)[index]);
		FREE(*mlpp);
		*mlpp = NULL;
	}

	FREE(listing);
	return ec;
}
