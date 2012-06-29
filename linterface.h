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
#ifndef UBER_LINTERFACE_H
#define UBER_LINTERFACE_H

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

#include <globus_common.h>

#include "errcode.h"
#include "ml.h"

typedef struct {
	void * ftppriv;
	void * unixpriv;
} pd_t;

typedef struct {
	errcode_t (*connect)(pd_t *, 
	                     char *  host, 
	                     int     port, 
	                     char *  user, 
	                     char *  pass, 
	                     char ** msg);
	int       (*connected)(pd_t *);
	errcode_t (*disconnect)(pd_t *, char ** msg);
	errcode_t (*retrvfile)(pd_t *, pd_t *, char *, globus_off_t, globus_off_t);
	errcode_t (*storfile)(pd_t *, 
	                      pd_t *, 
	                      char *, 
	                      int,
	                      globus_off_t, 
	                      globus_off_t);
	errcode_t (*appefile)(pd_t *);
	errcode_t (*read)(pd_t *, 
	                  char          ** buf, 
	                  globus_off_t  *  off, 
	                  size_t        *  len,
	                  int           *  eof);
	errcode_t (*write)(pd_t *, 
	                   char          * buf, 
	                   globus_off_t    off, 
	                   size_t          len,
	                   int             eof);
	errcode_t (*close)(pd_t *);
	errcode_t (*list)(pd_t *, char *);
	errcode_t (*pwd)(pd_t *, char **);
	errcode_t (*chdir)(pd_t *, char *);
	errcode_t (*chgrp)(pd_t *, char *, char *);
	errcode_t (*chmod)(pd_t *, int, char *);
	errcode_t (*mkdir)(pd_t *, char *);
	errcode_t (*rename)(pd_t *, char *, char *);
	errcode_t (*rm)(pd_t *, char *);
	errcode_t (*rmdir)(pd_t *, char *);
	errcode_t (*quote)(pd_t *, char *, char **);
	void (*mlsx_feats)(pd_t *, mf_t *);
	errcode_t (*stat)(pd_t *, char * path, ml_t **);
	errcode_t (*readdir)(pd_t *, char * path, ml_t ***, char * token);
	errcode_t (*size)(pd_t *, char * path, globus_off_t * size);
	errcode_t (*expand_tilde)(pd_t *, char * tilde, char ** fullpath);
	errcode_t (*stage) (pd_t *, char * file, int * staged);
	errcode_t (*cksum) (pd_t *, char * file, int * supported, unsigned int * crc);
	errcode_t (*link)(pd_t *, char * oldpath, char * newpath);
	errcode_t (*symlink)(pd_t *, char * oldpath, char * newpath);
	errcode_t (*utime)(pd_t *, char * path, time_t timestamp);
#ifdef SYSLOG_PERF
	char *    (*rhost) (pd_t *);
#endif /* SYSLOG_PERF */
} Linterface_t;

#endif /* UBER_LINTERFACE_H */
