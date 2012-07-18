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

#include "errcode.h"
#include "nc.h"

#ifdef DMALLOC
#include "dmalloc.h"
#endif /* DMALLOC */

static int
nc_connected(pd_t * pd)
{
	return 0;
}

static errcode_t
nc_disconnect(pd_t * pd, char ** msg)
{
	return ec_create(EC_GSI_SUCCESS, EC_GSI_SUCCESS, "Not connected.");
}

static errcode_t
nc_retr(pd_t * pd, 
        pd_t * opd,
        char * file, 
        globus_off_t off, 
        globus_off_t len)
{
	return ec_create(EC_GSI_SUCCESS, EC_GSI_SUCCESS, "Not connected.");
}

static errcode_t
nc_stor(pd_t * pd, 
        pd_t * opd, 
        char * file, 
        int    unique,
        globus_off_t off, 
        globus_off_t len)
{
	return ec_create(EC_GSI_SUCCESS, EC_GSI_SUCCESS, "Not connected.");
}

static errcode_t
nc_read(pd_t * pd, 
        char          ** buf,
        globus_off_t  *  off,
        size_t        *  len,
        int           *  eof)
{
	return ec_create(EC_GSI_SUCCESS, EC_GSI_SUCCESS, "Not connected.");
}

static errcode_t 
nc_write(pd_t * pd, 
         char          * buf, 
         globus_off_t    off, 
         size_t          len,
         int             eof)
{
	return ec_create(EC_GSI_SUCCESS, EC_GSI_SUCCESS, "Not connected.");
}

static errcode_t
nc_close(pd_t * pd)
{
	return EC_SUCCESS;
}

static errcode_t
nc_list(pd_t * pd, char * path)
{
	return ec_create(EC_GSI_SUCCESS, EC_GSI_SUCCESS, "Not connected.");
}

static errcode_t
nc_pwd(pd_t * pd, char ** path)
{
	return ec_create(EC_GSI_SUCCESS, EC_GSI_SUCCESS, "Not connected.");
}

static errcode_t
nc_chdir(pd_t * pd, char * path)
{
	return ec_create(EC_GSI_SUCCESS, EC_GSI_SUCCESS, "Not connected.");
}

static errcode_t
nc_chgrp(pd_t * pd, char * group, char * path)
{
	return ec_create(EC_GSI_SUCCESS, EC_GSI_SUCCESS, "Not connected.");
}

static errcode_t
nc_chmod(pd_t * pd, int perms, char * path)
{
	return ec_create(EC_GSI_SUCCESS, EC_GSI_SUCCESS, "Not connected.");
}

static errcode_t
nc_mkdir(pd_t * pd, char * path)
{
	return ec_create(EC_GSI_SUCCESS, EC_GSI_SUCCESS, "Not connected.");
}

static errcode_t
nc_rename(pd_t * pd, char * old, char * new)
{
	return ec_create(EC_GSI_SUCCESS, EC_GSI_SUCCESS, "Not connected.");
}

static errcode_t
nc_rm(pd_t * pd, char * path)
{
	return ec_create(EC_GSI_SUCCESS, EC_GSI_SUCCESS, "Not connected.");
}

static errcode_t
nc_rmdir(pd_t * pd, char * path)
{
	return ec_create(EC_GSI_SUCCESS, EC_GSI_SUCCESS, "Not connected.");
}

static errcode_t
nc_quote(pd_t * pd, char * cmd, char ** resp)
{
	return ec_create(EC_GSI_SUCCESS, EC_GSI_SUCCESS, "Not connected.");
}

static void
nc_mlsx_feats(pd_t * pd, mf_t * mf)
{
	memset(mf, 0xFF, sizeof(mf_t));
}

static errcode_t
nc_stat(pd_t * pd, char * path, ml_t ** ml)
{
	return ec_create(EC_GSI_SUCCESS, EC_GSI_SUCCESS, "Not connected.");
}

static errcode_t
nc_readdir(pd_t * pd, char * ppath, ml_t *** mlp, char * token)
{
	return ec_create(EC_GSI_SUCCESS, EC_GSI_SUCCESS, "Not connected.");
}

static errcode_t
nc_size(pd_t * pd, char * path, globus_off_t * size)
{
	return ec_create(EC_GSI_SUCCESS, EC_GSI_SUCCESS, "Not connected.");
}

static errcode_t 
nc_expand_tilde(pd_t * pd, char * tilde, char ** fullpath)
{
	return ec_create(EC_GSI_SUCCESS, EC_GSI_SUCCESS, "Not connected.");
}

static errcode_t 
nc_stage(pd_t * pd, char * path, int * staged)
{
	return ec_create(EC_GSI_SUCCESS, EC_GSI_SUCCESS, "Not connected.");
}

static errcode_t
nc_cksum (pd_t * pd, char * file, int * supported, unsigned int * crc)
{
	return ec_create(EC_GSI_SUCCESS, EC_GSI_SUCCESS, "Not connected.");
}

errcode_t 
nc_link(pd_t * pd, char * oldpath, char * newpath)
{
	return ec_create(EC_GSI_SUCCESS, EC_GSI_SUCCESS, "Not connected.");
}

errcode_t 
nc_symlink(pd_t * pd, char * oldpath, char * newpath)
{
	return ec_create(EC_GSI_SUCCESS, EC_GSI_SUCCESS, "Not connected.");
}

errcode_t 
nc_utime(pd_t * pd, char * path, time_t timestamp)
{
	return ec_create(EC_GSI_SUCCESS, EC_GSI_SUCCESS, "Not connected.");
}

errcode_t 
nc_lscos (pd_t * pd, char ** cos)
{
	return ec_create(EC_GSI_SUCCESS, EC_GSI_SUCCESS, "Not connected.");
}

errcode_t 
nc_lsfam (pd_t * pd, char ** families)
{
	return ec_create(EC_GSI_SUCCESS, EC_GSI_SUCCESS, "Not connected.");
}


#ifdef SYSLOG_PERF
char *
nc_rhost (pd_t * pd)
{
	return NULL;
}
#endif /* SYSLOG_PERF */

const Linterface_t NcInterface = {
	NULL, /* Connect */
	nc_connected,
	nc_disconnect,
	nc_retr,
	nc_stor,
	NULL, /* appefile */
	nc_read,
	nc_write,
	nc_close,
	nc_list,
	nc_pwd,
	nc_chdir,
	nc_chgrp,
	nc_chmod,
	nc_mkdir,
	nc_rename,
	nc_rm,
	nc_rmdir,
	nc_quote,
	nc_mlsx_feats,
	nc_stat,
	nc_readdir,
	nc_size,
	nc_expand_tilde,
	nc_stage,
	nc_cksum,
	nc_link,
	nc_symlink,
	nc_utime,
	nc_lscos,
	nc_lsfam,
#ifdef SYSLOG_PERF
	nc_rhost,
#endif /* SYSLOG_PERF */
};

