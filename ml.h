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
#ifndef UBER_ML_H
#define UBER_ML_H

#include "errcode.h"

typedef struct {
	unsigned int Type:1;
	unsigned int Size:1;
	unsigned int Modify:1;
	unsigned int Perm:1;
	unsigned int Charset:1;
	unsigned int UNIX_mode:1;
	unsigned int UNIX_owner:1;
	unsigned int UNIX_group:1;
	unsigned int Unique:1;
	unsigned int UNIX_slink:1;
	unsigned int X_family:1;
	unsigned int X_archive:1;
} mlsx_feats_t, mf_t;

typedef struct {
	mf_t   mf;
	int    type;
	char * name;
	char * UNIX_mode;
	char * UNIX_owner;
	char * UNIX_group;
	char * unique;
	char * UNIX_slink;
	char * X_family;
	char * X_archive;
	time_t modify;
	struct {
		unsigned int appe:1;
		unsigned int creat:1;
		unsigned int delete:1;
		unsigned int exec:1;
		unsigned int rename:1;
		unsigned int list:1;
		unsigned int mkdir:1;
		unsigned int purge:1;
		unsigned int retrieve:1;
		unsigned int store:1;
	} perms;
	globus_off_t size;
} mlsx_t, ml_t;

typedef struct ml_rec_store mlrs_t;


void ml_init(mlrs_t **);
void ml_destroy(mlrs_t *);
void ml_delete(ml_t *);
ml_t * ml_dup(ml_t *);

errcode_t ml_store_rec(mlrs_t *, ml_t *);
errcode_t ml_fetch_rec(mlrs_t *, ml_t **);

#endif /* UBER_ML_H */
