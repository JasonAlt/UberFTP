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
#ifndef UBER_LOGICAL_H
#define UBER_LOGICAL_H

#include <sys/types.h>
#include <time.h>

#include <globus_common.h>
#include "linterface.h"
#include "errcode.h"
#include "ml.h"

typedef struct logical_handle * lh_t;

lh_t l_init(Linterface_t li);

errcode_t l_connect(lh_t, 
                    char *  host, 
                    int     port, 
                    char *  user, 
                    char *  pass, 
                    char ** srvrmsg);

int l_connected(lh_t);
errcode_t l_disconnect(lh_t, char ** msg);

errcode_t l_retrvfile(lh_t, lh_t, char *, globus_off_t, globus_off_t);
errcode_t l_storfile(lh_t, lh_t, char *, int, globus_off_t, globus_off_t);
errcode_t l_appefile(lh_t);
errcode_t l_read(lh_t, 
                 char          ** buf, 
                 globus_off_t  *, 
                 size_t         *, 
                 int           * eof);
errcode_t l_write(lh_t,
                  char          * buf,
                  globus_off_t    off,
                  size_t          len,
                  int             eof);
errcode_t l_close(lh_t);
errcode_t l_list(lh_t, char * path);
errcode_t l_pwd(lh_t, char ** pwd);
errcode_t l_chdir(lh_t, char * path);
errcode_t l_chgrp(lh_t, char * group, char * path);
errcode_t l_chmod(lh_t, int perms, char * path);
errcode_t l_mkdir(lh_t, char * path);
errcode_t l_rename(lh_t, char * old, char * new);
errcode_t l_rm(lh_t, char *);
errcode_t l_rmdir(lh_t, char *);
errcode_t l_quote(lh_t, char *, char **);
void l_mlsx_feats(lh_t   lh, mf_t * mf);

/*
 * Attempt to return ml_t = NULL on no match
 * Not 100% effective but does provide a shortcut when it works.
 */
errcode_t l_stat(lh_t, char *, ml_t **);

/*
 * Return ml_t = NULL on no match but error if directory does not exist.
 */
errcode_t l_readdir(lh_t, char *, ml_t ***, char * token);
errcode_t l_size(lh_t, char * path, globus_off_t * size);
errcode_t l_expand_tilde(lh_t, char * path, char ** fullpath);
errcode_t l_stage(lh_t, char * path, int * staged);
errcode_t l_cksum(lh_t, char * file, int * supported, unsigned int * crc);
errcode_t l_link(lh_t, char * oldfile, char * newfile);
errcode_t l_symlink(lh_t, char * oldfile, char * newfile);
errcode_t l_utime(lh_t, char * path, time_t timestamp);
errcode_t l_lscos(lh_t, char **);
errcode_t l_lsfam(lh_t, char **);
#ifdef SYSLOG_PERF
char * l_rhost(lh_t);
#endif /* SYSLOG_PERF */

int l_supports_list(lh_t);
int l_supports_glob(lh_t lh);
int l_supports_recurse(lh_t lh);
int l_supports_mlsx(lh_t lh);
int l_is_ftp_service(lh_t lh);
int l_is_unix_service(lh_t lh);

#endif /* UBER_LOGICAL_H */
