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
#include <fnmatch.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "linterface.h"
#include "filetree.h"
#include "settings.h"
#include "logical.h"
#include "errcode.h"
#include "output.h"
#include "misc.h"
#include "ml.h"

#ifdef DMALLOC
#include "dmalloc.h"
#endif /* DMALLOC */


typedef struct file_tree_entry {
	struct file_tree_entry * parent;
	struct file_tree_entry * sibling;
	struct file_tree_entry * children;
	ml_t * ml;
	int    flags;
	errcode_t ec;
} fte_t;

struct file_tree_handle
{
	char * ipath;
	int    options;
	int    flags;
	lh_t   lh;
	mf_t   mf;
	fte_t  froot;
};

#define FTE_F_ROOT      0x01
#define FTE_F_RETURNED  0x02
#define FTE_F_GROWN     0x04
#define FTE_F_DESTROYED 0x08

#define FTH_F_ONE_RETURNED 0x01
#define FTH_F_TILDE_EXPAND 0x02
#define FTH_F_REGEXP       0x04

#define FT_S_RETURN       0x01
#define FT_S_DESTROY      0x02
#define FT_S_GROW         0x03
#define FT_S_EXPAND_TILDE 0x04

static fte_t * _ft_get_leaf(fth_t * fth);
static int     _ft_next_state(fth_t * fth, fte_t * leaf);
static int     _ft_leaf_depth(fte_t * leaf);
static char *  _ft_path(fte_t * fte);
static void    _ft_grow_leaf(fth_t *, fte_t * leaf);
static void    _ft_stat(fth_t * fth, fte_t * leaf, char * token);
static void    _ft_readdir(fth_t * fth, fte_t * leaf, char * token);
static void    _ft_delete_br(fth_t *, fte_t * leaf);
static void    _ft_del_fte(fte_t * ftep);
static void    _ft_expand_tilde(fth_t * fth, fte_t * ftep);
static int     _ft_should_return(fth_t * fth, fte_t * leaf);
static int     _ft_should_grow(fth_t * fth, fte_t * leaf);

fth_t * 
ft_init(lh_t lh, char * ipath, int options)
{
	fth_t * fth = NULL;

	fth = (fth_t *) malloc(sizeof(fth_t));
	memset(fth, 0, sizeof(fth_t));
	fth->options = options;
	fth->ipath   = Strdup(ipath);
	fth->lh      = lh;
	l_mlsx_feats(lh, &fth->mf);
	fth->froot.flags = FTE_F_ROOT;


	return fth;
}

void
ft_destroy(fth_t * fth)
{
	fte_t * leaf = NULL;

	if (!fth)
		return;

	while ((leaf = _ft_get_leaf(fth)) != NULL)
		_ft_delete_br(fth, leaf);

	FREE(fth->ipath);
	FREE(fth);
}

/* 
 * This will return ec = EC_SUCCESS and *mlp = NULL
 * on no match. It will return *mlp = root when
 * mlst is not supported.
 */
errcode_t
ft_get_next_ft(fth_t * fth, ml_t ** mlp, int options)
{
	errcode_t ec  = EC_SUCCESS;
	fte_t * leaf  = NULL;

	*mlp = NULL;

	while ((leaf = _ft_get_leaf(fth)) != NULL)
	{
		switch (_ft_next_state(fth, leaf))
		{
		case FT_S_EXPAND_TILDE:
			_ft_expand_tilde(fth, leaf);
			fth->flags |= FTH_F_TILDE_EXPAND;
			continue;

		case FT_S_RETURN:
			break;

		case FT_S_DESTROY:
			_ft_delete_br(fth, leaf);
			continue;

		case FT_S_GROW:
			_ft_grow_leaf(fth, leaf);
			leaf->flags |= FTE_F_GROWN;
			continue;
		}

		if (!(options & FTH_O_PEAK))
			leaf->flags |= FTE_F_RETURNED;

		break;
	}

	if (leaf && leaf->ec)
		ec = ec_dup(leaf->ec);

	if (!leaf && !ec)
	{
		if (options & FTH_O_ERR_NO_MATCH)
		{
			if (! (fth->flags & FTH_F_ONE_RETURNED))
				ec = ec_create(EC_GSI_SUCCESS,
				               EC_GSI_SUCCESS,
				               "No match for %s",
				               fth->ipath);
		}
	}

	if (leaf || ec)
		fth->flags |= FTH_F_ONE_RETURNED;

	if (leaf && leaf->ml && !ec)
	{
		*mlp = ml_dup(leaf->ml);
		FREE((*mlp)->name);
		(*mlp)->name = _ft_path(leaf);
	}

	if (leaf && !leaf->ml && !ec)
	{
		*mlp = (ml_t *) malloc(sizeof(ml_t));
		memset(*mlp, 0, sizeof(ml_t));
		(*mlp)->name = Strdup(fth->ipath);
	}

	return ec;
}

static fte_t *
_ft_get_leaf(fth_t * fth)
{
	fte_t * leaf   = NULL;

	leaf = &fth->froot;
	if (fth->froot.children == NULL && fth->froot.sibling != NULL)
		leaf = fth->froot.sibling;
	for (; leaf && leaf->children; leaf = leaf->children);

	if (leaf->flags & FTE_F_ROOT && leaf->flags & FTE_F_DESTROYED)
		return NULL;

	return leaf;
}

static int
_ft_next_state(fth_t * fth, fte_t * leaf)
{
	/* Tilde expansion first. */
	if (*fth->ipath == '~' && !(fth->flags & FTH_F_TILDE_EXPAND))
		return FT_S_EXPAND_TILDE;

	/* Always return unreturned errors. */
	if (leaf->ec && !(leaf->flags & FTE_F_RETURNED))
		return FT_S_RETURN;

	/* Destroy if the error has been returned. */
	if (leaf->ec)
		return FT_S_DESTROY;

	if (!(fth->options & FTH_O_REVERSE))
	{
		if (_ft_should_return(fth, leaf))
			return FT_S_RETURN;
		if (_ft_should_grow(fth, leaf))
			return FT_S_GROW;
	} else
	{
		if (_ft_should_grow(fth, leaf))
			return FT_S_GROW;
		if (_ft_should_return(fth, leaf))
			return FT_S_RETURN;
	}

	return FT_S_DESTROY;
}

/*
 * leaf depth starts with 0 (root leaf)
 */
static int
_ft_leaf_depth(fte_t * leaf)
{
	int depth = 0;

	for (; !(leaf->flags & FTE_F_ROOT); depth++, leaf = leaf->parent);
	return depth;
}

/*
 * Total number of path componets.
 */
static int
_ft_path_comps(fth_t * fth)
{
    char * token = NULL;
    char * cptr  = NULL;
    char * scptr = NULL;
	char * path  = fth->ipath;
    int    depth = 0;

    if (!path)
        return depth;

    if (*path == '|')
        return depth;

    if (*path == '/')
        depth++;

    cptr = scptr = Strdup(path);

    for (;(token = strtok(cptr, "/")); depth++, cptr = NULL);

    FREE(scptr);
    return depth;
}

static char *
_ft_path(fte_t * fte)
{
	char * path = NULL;

	for (; !(fte->flags & FTE_F_ROOT) ; fte = fte->parent)
		path = MakePath(fte->ml->name, path);
	return path;
}

static void
_ft_grow_leaf(fth_t * fth, fte_t * leaf)
{
	char * token    = NULL;
	int    depth    = _ft_leaf_depth(leaf);
	int    numcomps = _ft_path_comps(fth);

	/* Ignore non directories. */
	if (leaf->ml && leaf->ml->type != S_IFDIR)
		return;

	/* Ignore piped commands. */
	if (*(fth->ipath) == '|')
		return;

	/* Cant grow it if growing isnt supported. */
	if (!l_supports_mlsx(fth->lh))
		return;

	token = PathTok(fth->ipath, depth);
	switch (!token || (IsGlob(token) && s_glob()))
	{
	case 1:
		_ft_readdir(fth, leaf, token);
		fth->flags |= FTH_F_REGEXP;
		break;

	case 0:
		if (depth == (numcomps - 1) || fth->flags & FTH_F_REGEXP)
		{
			_ft_stat(fth, leaf, token);
			break;
		}

		/* Just fake this leaf */
		leaf->children = (fte_t *) malloc(sizeof(fte_t));
		memset(leaf->children, 0, sizeof(fte_t));
		leaf->children->parent = leaf;
		leaf->children->ml = (ml_t *) malloc(sizeof(ml_t));
		memset(leaf->children->ml, 0, sizeof(ml_t));
		leaf->children->ml->type = S_IFDIR;
		leaf->children->ml->name = Strdup(token);
		break;
	}

	FREE(token);
}

static void
_ft_stat(fth_t * fth, fte_t * leaf, char * token)
{
	char * path  = NULL;
	char * cptr  = NULL;
	ml_t * mlp   = NULL;
	errcode_t ec = EC_SUCCESS;

	path = MakePath((cptr = _ft_path(leaf)), token);
	ec   = l_stat(fth->lh, path, &mlp);

	FREE(cptr);
	FREE(path);

	if (ec)
	{
		leaf->ec = ec;
	} else if (mlp)
	{
		leaf->children = (fte_t *) malloc(sizeof(fte_t));
		memset(leaf->children, 0, sizeof(fte_t));
		leaf->children->parent = leaf;
		leaf->children->ml     = mlp;
		FREE(leaf->children->ml->name);
		leaf->children->ml->name = Strdup(token);
	}
}

static void
_ft_readdir(fth_t * fth, fte_t * leaf, char * token)
{
	errcode_t ec   = EC_SUCCESS;
	char   *  path = NULL;
	fte_t **  ftep = &leaf->children;
	ml_t  **  mlp  = NULL;
	ml_t  **  sml  = NULL;

	path = _ft_path(leaf);
	ec = l_readdir(fth->lh, path, &mlp, token);
	if (ec)
	{
		leaf->ec = ec;
		leaf->flags &= ~FTE_F_RETURNED;
		goto finish;
	}

	for (sml = mlp; mlp && *mlp; mlp++)
	{
		if (strcmp((*mlp)->name, ".") == 0)
		{
			ml_delete(*mlp);
			continue;
		}

		if (strcmp((*mlp)->name, "..") == 0)
		{
			ml_delete(*mlp);
			continue;
		}

		/*
		 * The lower layer does the token matching (if the token is non NULL)
		 * for performance reasons.
		 */

		*ftep = (fte_t *) malloc(sizeof(fte_t));
		memset(*ftep, 0, sizeof(fte_t));
		(*ftep)->ml = *mlp;
		(*ftep)->parent = leaf;

		ftep = &((*ftep)->sibling);
	}

finish:
	FREE(path);
	FREE(sml);
}

static void
_ft_delete_br(fth_t * fth, fte_t * leaf)
{
	fte_t *  pfte = NULL;
	fte_t ** sfte = NULL;

	if (leaf->flags & FTE_F_ROOT)
	{
		leaf->flags |= FTE_F_DESTROYED;
		return;
	}

	pfte = leaf->parent;

	for (sfte = &pfte->children; *sfte != leaf; sfte = &(*sfte)->sibling);

	*sfte = leaf->sibling;
	
	if (!pfte->children && !(fth->options & FTH_O_REVERSE))
		_ft_delete_br(fth, pfte);

	_ft_del_fte(leaf);
}

static void
_ft_del_fte(fte_t * ftep)
{
	if (!ftep)
		return;

	ec_destroy(ftep->ec);
	ml_delete(ftep->ml);
	FREE(ftep);
}

static void
_ft_expand_tilde(fth_t * fth, fte_t * ftep)
{
	char * spath = fth->ipath;
	char * fpath = NULL;

	ftep->ec = l_expand_tilde(fth->lh, spath, &fpath);

	if (fpath)
	{
		fth->ipath = fpath;
		FREE(spath);
	}
}

static int
_ft_should_return(fth_t * fth, fte_t * leaf)
{
	int depth    = _ft_leaf_depth(leaf);
	int numcomps = _ft_path_comps(fth);

	/*
	 * If this leaf hasn't been returned...
	 */
	if (!(leaf->flags & FTE_F_RETURNED))
	{
		/* Return it if the service doesnt support growing. This is the root. */
		if (!l_supports_mlsx(fth->lh))
			return 1;

		/* Don't return depth < numcomps */

		/* If its the last token... */
		if (depth == numcomps)
		{
			/*
			 * If it's a directory...
			 * If we got here without a ml, then it is
			 * probably a pipe command or something similar.
			 */
			if (leaf->ml && leaf->ml->type == S_IFDIR)
			{
				/* Return it if we don't expand it. */
				if (! (fth->options & FTH_O_EXPAND_DIR))
					return 1;
			} else
			{
				/* Return non directory elements. */
				return 1;
			}
		}

		/* Return anything we took the trouble to grow. */
		if (depth > numcomps)
			return 1;
	}

	return 0;
}

static int
_ft_should_grow(fth_t * fth, fte_t * leaf)
{
	int depth    = _ft_leaf_depth(leaf);
	int numcomps = _ft_path_comps(fth);

	/* If this leaf hasn't been grown... */
	if (!(leaf->flags & FTE_F_GROWN))
	{
		/* Grow root */
		if (leaf->flags & FTE_F_ROOT)
			return 1;

		/* If it's a directory... */
		if (leaf->ml->type == S_IFDIR)
		{
			/* Grow it if we are shallow. */
			if (depth < numcomps)
				return 1;

			/* If it is the last token. */
			if (depth == numcomps)
			{
				/* Grow it if we expand directories. */
				if (fth->options & FTH_O_EXPAND_DIR)
					return 1;
				/* Grow it if we are recusring. */
				if (fth->options & FTH_O_RECURSE)
					return 1;
			}

			if (depth > numcomps)
			{
				/* Grow it if we are recusring. */
				if (fth->options & FTH_O_RECURSE)
					return 1;
			}
		}
	}
	return 0;
}
