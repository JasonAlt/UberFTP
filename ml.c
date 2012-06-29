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

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <globus_common.h>

#include "settings.h"
#include "errcode.h"
#include "logical.h"
#include "misc.h"
#include "ml.h"

struct ml_rec_store
{
	int fd;
	globus_off_t woff;
};

typedef struct ml_cptr {
	int off; /* From start of record. */
	int len;
} mlcptr_t;

typedef struct ml_rec {
	int      length;
	mf_t     mf;
    int      type;
	mlcptr_t name;
	mlcptr_t UNIX_mode;
	mlcptr_t UNIX_owner;
	mlcptr_t UNIX_group;
	mlcptr_t unique;
	mlcptr_t UNIX_slink;
	mlcptr_t X_family;
	mlcptr_t X_archive;
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
} mlr_t;

static mlr_t *
_ml_pack_cptr(mlr_t * mlr, mlcptr_t * mlcptr, char * str);

static char *
_ml_unpack_cptr(mlr_t * mlr, mlcptr_t * mlcptr);

static errcode_t
_ml_order_recs(mlrs_t * mlrs, mlr_t ** mlr);

static errcode_t
_ml_read_rec(mlrs_t * mlrs, mlr_t ** mlrp);

static errcode_t
_ml_write_rec(mlrs_t * mlrs, mlr_t * mlr);

static errcode_t
_ml_swap_recs(mlrs_t * mlrs, mlr_t * mlr1, mlr_t * mlr2);

void 
ml_init(mlrs_t ** mlrs)
{
	*mlrs = (mlrs_t*) malloc(sizeof(mlrs_t));
	memset(*mlrs, 0, sizeof(mlrs_t));

	(*mlrs)->fd = -1;
}

void
ml_destroy(mlrs_t * mlrs)
{
	if (!mlrs)
		return;

	if (mlrs->fd != -1)
		close(mlrs->fd);
	FREE(mlrs);
}

void
ml_delete(ml_t * ml)
{
	if (!ml)
		return;

	FREE(ml->name);
	FREE(ml->UNIX_mode);
	FREE(ml->UNIX_owner);
	FREE(ml->UNIX_group);
	FREE(ml->UNIX_slink);
	FREE(ml->X_family);
	FREE(ml->X_archive);
	FREE(ml->unique);
	FREE(ml);
}

ml_t * 
ml_dup(ml_t * mlp)
{
	ml_t * rptr = NULL;

	if (!mlp)
		return NULL;

	rptr = (ml_t *) malloc(sizeof(ml_t));
	memcpy(rptr, mlp, sizeof(ml_t));
	rptr->name       = Strdup(mlp->name);
	rptr->UNIX_mode  = Strdup(mlp->UNIX_mode);
	rptr->UNIX_owner = Strdup(mlp->UNIX_owner);
	rptr->UNIX_group = Strdup(mlp->UNIX_group);
	rptr->UNIX_slink = Strdup(mlp->UNIX_slink);
	rptr->X_family   = Strdup(mlp->X_family);
	rptr->X_archive  = Strdup(mlp->X_archive);
	rptr->unique     = Strdup(mlp->unique);
	return rptr;
}


errcode_t
ml_store_rec(mlrs_t * mlrs, ml_t * mlp)
{
	mlr_t * mlr = NULL;
	char    template[] = "/tmp/UberFTPXXXXXX";
	errcode_t ec = EC_SUCCESS;

	if (mlrs->fd == -1)
	{
		mlrs->fd = mkstemp(template);
		if (mlrs->fd == -1)
			return ec_create(EC_GSI_SUCCESS,
			                 EC_GSI_SUCCESS,
			                 "Failed to create temporary file: %s",
			                 strerror(errno));
		unlink(template);
	}

	mlr = (mlr_t *) malloc(sizeof(mlr_t));
	memset(mlr, 0, sizeof(mlr_t));
	mlr->length = sizeof(mlr_t);
	mlr->mf       = mlp->mf;
	mlr->type     = mlp->type;
	mlr->size     = mlp->size;
	mlr->modify   = mlp->modify;
	mlr->perms.appe     = mlp->perms.appe;
	mlr->perms.creat    = mlp->perms.creat;
	mlr->perms.delete   = mlp->perms.delete;
	mlr->perms.exec     = mlp->perms.exec;
	mlr->perms.rename   = mlp->perms.rename;
	mlr->perms.list     = mlp->perms.list;
	mlr->perms.mkdir    = mlp->perms.mkdir;
	mlr->perms.purge    = mlp->perms.purge;
	mlr->perms.retrieve = mlp->perms.retrieve;
	mlr->perms.store    = mlp->perms.store;

	mlr = _ml_pack_cptr(mlr, &mlr->name, mlp->name);
	mlr = _ml_pack_cptr(mlr, &mlr->UNIX_mode, mlp->UNIX_mode);
	mlr = _ml_pack_cptr(mlr, &mlr->UNIX_owner, mlp->UNIX_owner);
	mlr = _ml_pack_cptr(mlr, &mlr->UNIX_group, mlp->UNIX_group);
	mlr = _ml_pack_cptr(mlr, &mlr->unique, mlp->unique);
	mlr = _ml_pack_cptr(mlr, &mlr->UNIX_slink, mlp->UNIX_slink);
	mlr = _ml_pack_cptr(mlr, &mlr->X_family, mlp->X_family);
	mlr = _ml_pack_cptr(mlr, &mlr->X_archive, mlp->X_archive);

	lseek(mlrs->fd, mlrs->woff, SEEK_SET);
	ec = _ml_write_rec(mlrs, mlr);
	mlrs->woff = lseek(mlrs->fd, 0, SEEK_CUR);
	FREE(mlr);
	return ec;
}

errcode_t
ml_fetch_rec(mlrs_t * mlrs, ml_t ** mlp)
{
	errcode_t ec = EC_SUCCESS;
	mlr_t  * mlr = NULL;

	*mlp = NULL;

	ec = _ml_order_recs(mlrs, &mlr);
	if (ec || !mlr)
		return ec;

	*mlp = (ml_t *) malloc(sizeof(ml_t));

	memset(*mlp, 0, sizeof(ml_t));
	(*mlp)->type           = mlr->type;
	(*mlp)->mf             = mlr->mf;
	(*mlp)->size           = mlr->size;
	(*mlp)->modify         = mlr->modify;
	(*mlp)->perms.appe     = mlr->perms.appe;
	(*mlp)->perms.creat    = mlr->perms.creat;
	(*mlp)->perms.delete   = mlr->perms.delete;
	(*mlp)->perms.exec     = mlr->perms.exec;
	(*mlp)->perms.rename   = mlr->perms.rename;
	(*mlp)->perms.list     = mlr->perms.list;
	(*mlp)->perms.mkdir    = mlr->perms.mkdir;
	(*mlp)->perms.purge    = mlr->perms.purge;
	(*mlp)->perms.retrieve = mlr->perms.retrieve;
	(*mlp)->perms.store    = mlr->perms.store;

	(*mlp)->name       = _ml_unpack_cptr(mlr, &mlr->name);
	(*mlp)->UNIX_mode  = _ml_unpack_cptr(mlr, &mlr->UNIX_mode);
	(*mlp)->UNIX_owner = _ml_unpack_cptr(mlr, &mlr->UNIX_owner);
	(*mlp)->UNIX_group = _ml_unpack_cptr(mlr, &mlr->UNIX_group);
	(*mlp)->unique     = _ml_unpack_cptr(mlr, &mlr->unique);
	(*mlp)->UNIX_slink = _ml_unpack_cptr(mlr, &mlr->UNIX_slink);
	(*mlp)->X_archive  = _ml_unpack_cptr(mlr, &mlr->X_archive);
	(*mlp)->X_family   = _ml_unpack_cptr(mlr, &mlr->X_family);

	FREE(mlr);
	return EC_SUCCESS;
}

static mlr_t *
_ml_pack_cptr(mlr_t * mlr, mlcptr_t * mlcptr, char * str)
{
	mlcptr->off = 0;
	mlcptr->len = 0;

	if (!str)
		return mlr;

	mlcptr->len = strlen(str);
	mlcptr->off = mlr->length;

	mlr = (mlr_t *) realloc(mlr, mlr->length + mlcptr->len);

	/* mlcptr no longer valid after realloc() */
	memcpy(((char *)mlr) + mlr->length, str, strlen(str));
	mlr->length += strlen(str);

	return mlr;
}

static char *
_ml_unpack_cptr(mlr_t * mlr, mlcptr_t * mlcptr)
{
	if (mlcptr->len == 0)
		return NULL;

	return Strndup(((char*)mlr) + mlcptr->off, mlcptr->len);
}

static errcode_t
_ml_order_recs(mlrs_t * mlrs, mlr_t ** mlr)
{
	char *  name1 = NULL;
	char *  name2 = NULL;
	mlr_t * mlr1  = NULL;
	mlr_t * mlr2  = NULL;
	errcode_t ec  = EC_SUCCESS;

	*mlr = NULL;

	/* rewind */
	lseek(mlrs->fd, 0, SEEK_SET);

	while (lseek(mlrs->fd, 0, SEEK_CUR) < mlrs->woff)
	{
		if (!mlr1)
		{
			ec = _ml_read_rec(mlrs, &mlr1);
			if (ec)
				break;
			continue;
		}

		ec = _ml_read_rec(mlrs, &mlr2);
		if (ec)
			break;

		switch (s_order())
		{
		case ORDER_BY_NAME:
			name1 = Strndup(((char *)mlr1) + mlr1->name.off, mlr1->name.len);
			name2 = Strndup(((char *)mlr2) + mlr2->name.off, mlr2->name.len);
			if (strcmp(name1, name2) < 0)
			{
				ec = _ml_swap_recs(mlrs, mlr1, mlr2);
				FREE(mlr2);
			}
			else
			{
				FREE(mlr1);
				mlr1 = mlr2;
			}

			FREE(name1);
			FREE(name2);
			break;
		case ORDER_BY_SIZE:
			if (mlr1->size < mlr2->size)
			{
				ec = _ml_swap_recs(mlrs, mlr1, mlr2);
				FREE(mlr2);
			} else
			{
				FREE(mlr1);
				mlr1 = mlr2;
			}
			break;
		case ORDER_BY_TYPE:
			if (mlr1->type > mlr2->type)
			{
				ec = _ml_swap_recs(mlrs, mlr1, mlr2);
			} else
			{
				FREE(mlr1);
				mlr1 = mlr2;
			}
			break;
		case ORDER_BY_NONE:
			FREE(mlr1);
			mlr1 = mlr2;
			mlr2 = NULL;
			break;
		}
		if (ec)
			break;
	}

	if (mlr1)
	{
		mlrs->woff -= mlr1->length;
		*mlr = mlr1;
	}

	return ec;
}

static errcode_t
_ml_read_rec(mlrs_t * mlrs, mlr_t ** mlrp)
{
	ssize_t cnt = 0;
	size_t  off = 0;
	mlr_t * mlr = NULL;

	mlr = *mlrp = (mlr_t *) malloc(sizeof(mlr_t));
	for (off = 0; off < sizeof(mlr->length) ; off += cnt)
	{
		cnt = read(mlrs->fd, ((char*)mlr) + off, sizeof(mlr->length) - off);
		if (cnt == -1)
		{
			FREE(mlr);
			return ec_create(EC_GSI_SUCCESS,
			                 EC_GSI_SUCCESS,
			                 "Failure while reading record: %s",
			                 strerror(errno));
		}
	}

	mlr = *mlrp = (mlr_t *) realloc(mlr, mlr->length);
	for (; off < mlr->length ; off += cnt)
	{
		cnt = read(mlrs->fd, ((char*)mlr) + off, mlr->length - off);
		if (cnt == -1)
		{
			FREE(mlr);
			return ec_create(EC_GSI_SUCCESS,
			                 EC_GSI_SUCCESS,
			                 "Failure while reading record: %s",
			                 strerror(errno));
		}
	}
	return EC_SUCCESS;
}

static errcode_t
_ml_write_rec(mlrs_t * mlrs, mlr_t * mlr)
{
	size_t  off = 0;
	ssize_t cnt = 0;

	for (off = 0; off < mlr->length; off += cnt)
	{
		cnt = write(mlrs->fd, mlr + off, mlr->length - off);
		if (cnt == -1)
		{
			FREE(mlr);
			return ec_create(EC_GSI_SUCCESS,
			                 EC_GSI_SUCCESS,
			                 "Failed to write ML record to file: %s",
			                 strerror(errno));
		}
	}
	return EC_SUCCESS;
}

static errcode_t
_ml_swap_recs(mlrs_t * mlrs, mlr_t * mlr1, mlr_t * mlr2)
{
	errcode_t ec = EC_SUCCESS;

	lseek(mlrs->fd, 0-mlr1->length, SEEK_CUR);
	lseek(mlrs->fd, 0-mlr2->length, SEEK_CUR);

	ec = _ml_write_rec(mlrs, mlr2);
	if (ec)
		return ec;
	ec = _ml_write_rec(mlrs, mlr1);
	return ec;
}
