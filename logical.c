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
#include <sys/types.h>
#include <stdlib.h>
#include <time.h>

#include "linterface.h"
#include "logical.h"
#include "unix.h"
#include "ftp.h"

#ifdef DMALLOC
#include "dmalloc.h"
#endif /* DMALLOC */


struct logical_handle {
	Linterface_t li;
	Linterface_t li_uc;
	pd_t privdata;
};

lh_t 
l_init(Linterface_t li)
{
	lh_t lh = (lh_t) malloc(sizeof(struct logical_handle));

	lh->li    = li;
	lh->li_uc = li;
	lh->li.connect  = FtpInterface.connect;
	lh->privdata.ftppriv  = NULL;
	lh->privdata.unixpriv = NULL;
	return lh;
}

errcode_t 
l_connect(lh_t    lh, 
          char *  host, 
          int     port, 
          char *  user, 
          char *  pass, 
          char ** srvrmsg)
{
	errcode_t errcode = EC_SUCCESS;

	errcode = lh->li.connect(&lh->privdata, 
	                          host, 
	                          port, 
	                          user, 
	                          pass, 
	                          srvrmsg);

	if (errcode == EC_SUCCESS)
		lh->li = FtpInterface;

	return errcode;
}

int
l_connected(lh_t lh)
{
	return lh->li.connected(&lh->privdata);
}

errcode_t 
l_disconnect(lh_t lh, char ** msg)
{
	errcode_t errcode = EC_SUCCESS;

	errcode = lh->li.disconnect(&lh->privdata, msg);

	if (errcode == EC_SUCCESS)
	{
		lh->li = lh->li_uc;
		lh->li.connect = FtpInterface.connect;
	}

	return errcode;
}

errcode_t
l_retrvfile(lh_t lh, 
            lh_t olh, 
            char * file, 
            globus_off_t off, 
            globus_off_t len)
{
	return lh->li.retrvfile(&lh->privdata,
	                        olh ? &olh->privdata : NULL,
	                        file,
	                        off,
	                        len);
}

errcode_t
l_storfile(lh_t lh, 
           lh_t olh, 
           char * file, 
           int    unique,
           globus_off_t off, 
           globus_off_t len)
{
	return lh->li.storfile(&lh->privdata,
	                       olh ? &olh->privdata : NULL,
	                       file,
	                       unique,
	                       off,
	                       len);
}

errcode_t
l_appefile(lh_t);

errcode_t
l_read(lh_t lh, 
       char          ** buf, 
       globus_off_t  *  off,
       size_t        *  len,
       int           *  eof)
{
	return lh->li.read(&lh->privdata,
	                    buf,
	                    off,
	                    len,
	                    eof);
}

errcode_t
l_write(lh_t lh,
        char          * buf,
        globus_off_t    off,
        size_t          len,
        int             eof)
{
	return lh->li.write(&lh->privdata,
	                    buf,
	                    off,
	                    len,
	                    eof);
}

errcode_t
l_close(lh_t lh)
{
	return lh->li.close(&lh->privdata);
	                     
}

errcode_t 
l_list(lh_t lh, char * path)
{
	return lh->li.list(&lh->privdata, path);
}

errcode_t
l_pwd(lh_t lh, char ** path)
{
	return lh->li.pwd(&lh->privdata, path);
}

errcode_t
l_chdir(lh_t lh, char * path)
{
	return lh->li.chdir(&lh->privdata, path);
}

errcode_t
l_chgrp(lh_t lh, char * group, char * path)
{
	return lh->li.chgrp(&lh->privdata, group, path);
}

errcode_t
l_chmod(lh_t lh, int perms, char * path)
{
	return lh->li.chmod(&lh->privdata, perms, path);
}

errcode_t
l_mkdir(lh_t lh, char * path)
{
	return lh->li.mkdir(&lh->privdata, path);
}

errcode_t
l_rename(lh_t lh, char * old, char * new)
{
	return lh->li.rename(&lh->privdata, old, new);
}

errcode_t
l_rm(lh_t lh, char * path)
{
	return lh->li.rm(&lh->privdata, path);
}

errcode_t
l_rmdir(lh_t lh, char * path)
{
	return lh->li.rmdir(&lh->privdata, path);
}

errcode_t
l_quote(lh_t lh, char * cmd, char ** resp)
{
	return lh->li.quote(&lh->privdata, cmd, resp);
}

void
l_mlsx_feats(lh_t lh, mf_t * mf)
{
	lh->li.mlsx_feats(&lh->privdata, mf);
}

errcode_t
l_stat(lh_t lh, char * path, ml_t ** ml)
{
	return lh->li.stat(&lh->privdata, path, ml);
}

errcode_t
l_readdir(lh_t lh, char * path, ml_t *** mlp, char * token)
{
	return lh->li.readdir(&lh->privdata, path, mlp, token);
}

errcode_t
l_size(lh_t lh, char * path, globus_off_t * size)
{
	return lh->li.size(&lh->privdata, path, size);
}

errcode_t
l_expand_tilde(lh_t lh, char * path, char ** fullpath)
{
	return lh->li.expand_tilde(&lh->privdata, path, fullpath);
}

errcode_t
l_stage(lh_t lh, char * path, int * staged)
{
	return lh->li.stage(&lh->privdata, path, staged);
}

errcode_t
l_cksum(lh_t lh, char * file, int * supported, unsigned int * crc)
{
	return lh->li.cksum(&lh->privdata, file, supported, crc);
}

errcode_t
l_link(lh_t lh, char * oldfile, char * newfile)
{
	return lh->li.link(&lh->privdata, oldfile, newfile);
}

errcode_t
l_symlink(lh_t lh, char * oldfile, char * newfile)
{
	return lh->li.symlink(&lh->privdata, oldfile, newfile);
}

errcode_t
l_utime(lh_t lh, char * path, time_t timestamp)
{
	return lh->li.utime(&lh->privdata, path, timestamp);
}

#ifdef SYSLOG_PERF
char *
l_rhost (lh_t lh)
{
    return lh->li.rhost(&lh->privdata);
}
#endif /* SYSLOG_PERF */


int
l_supports_list(lh_t lh)
{
	mf_t mf;
	l_mlsx_feats(lh, &mf);

	if (mf.UNIX_mode && mf.UNIX_owner && mf.Size && mf.Modify)
		return 1;
	return 0;
}

/*
 * These funcitons basically do the same thing, but they are used
 * in different contexts to keep things clear. It is possible that
 * at some point in the future they might actually check different
 * criteria.
 */
int
l_supports_recurse(lh_t lh)
{
	mf_t mf;
	l_mlsx_feats(lh, &mf);
	return mf.Type;
}

int
l_supports_glob(lh_t lh)
{
	mf_t mf;
	l_mlsx_feats(lh, &mf);
	return mf.Type;
}

int
l_supports_mlsx(lh_t lh)
{
	mf_t mf;
	l_mlsx_feats(lh, &mf);
	return mf.Type;
}

int
l_is_unix_service(lh_t lh)
{
	return (lh->li.disconnect == UnixInterface.disconnect);
}

int
l_is_ftp_service(lh_t lh)
{
	return (lh->li.disconnect == FtpInterface.disconnect);
}
