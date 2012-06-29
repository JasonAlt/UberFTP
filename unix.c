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

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fnmatch.h>
#include <unistd.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <pwd.h>
#include <grp.h>

#include "settings.h"
#include "errcode.h"
#include "cksum.h"
#include "unix.h"
#include "misc.h"

#ifdef DMALLOC
#include "dmalloc.h"
#endif /* DMALLOC */

typedef struct {
	int fd;
	globus_off_t len;
	globus_off_t off;
	pid_t        pid;
	char       * opwd;
} uh_t;

static errcode_t
_unix_mlsx(char * ppath, char * path, ml_t ** mlp);

static uh_t * _unix_init(uh_t * uh);
#ifdef NOT
static void _unix_destroy(pd_t * pd);
#endif /* NOT */

int
unix_connected(pd_t * pd)
{
	return 0;
}

static errcode_t
unix_disconnect(pd_t * pd, char ** msg)
{
	*msg = NULL;
	return ec_create(EC_GSI_SUCCESS, EC_GSI_SUCCESS, "Not connected.");
}

static errcode_t
unix_retr(pd_t * pd, 
          pd_t * opd,
          char * file, 
          globus_off_t off, 
          globus_off_t len)
{
	int    ret = 0;
	int    fds[2];
	uh_t * uh  = NULL;
	globus_off_t noff = 0;
	errcode_t ec = EC_SUCCESS;

	uh = pd->unixpriv = _unix_init(pd->unixpriv);
	uh->len = len;
	uh->off = off;

	if (off == (globus_off_t)-1)
		uh->off = 0;

	if (*file == '|')
	{
		if (off == (globus_off_t)-1)
			uh->len = -1;

		if (pipe(fds))
			return ec_create(EC_GSI_SUCCESS,
			                 EC_GSI_SUCCESS,
			                 "pipe failed: %s",
			                 strerror(errno));

		uh->pid = fork();
		if (uh->pid == -1)
		{
			close(fds[0]);
			close(fds[1]);
			return ec_create(EC_GSI_SUCCESS,
			                 EC_GSI_SUCCESS,
			                 "Failed to fork process: %s",
			                 strerror(errno));
		}

		if (!uh->pid)
		{
			for (file++; *file != '\0' && isspace(*file); file++);
			close(fds[0]);
			dup2(fds[1], 1);
			/* dup2(fds[1], 2); */
			execl("/bin/sh", "sh", "-c", file, NULL);
			execlp("sh", "sh", "-c", file, NULL);
			exit (1);
		}

		close(fds[1]);
		uh->fd = fds[0];
		return EC_SUCCESS;
	}

	/* Regular file. */
	ret = access(file, R_OK);
	if (ret)
		return ec_create(EC_GSI_SUCCESS, 
		                 EC_GSI_SUCCESS,
		                 "Access to %s failed: %s",
		                 file,
		                 strerror(errno));


	uh->fd = open(file, O_RDONLY);
	if (uh->fd == -1)
	{
		ec = ec_create(EC_GSI_SUCCESS,
		               EC_GSI_SUCCESS,
		               "Failed to open %s: %s",
		               file,
		               strerror(errno));
		goto finish;
	}

	if (off != (globus_off_t)-1)
	{
		noff = lseek(uh->fd, off, SEEK_SET);
		if (noff != off)
			ec = ec_create(EC_GSI_SUCCESS,
			               EC_GSI_SUCCESS,
			               "Failed to seek file: %s",
			               strerror(errno));
	}

finish:
	return ec;
}

static errcode_t
unix_stor(pd_t * pd, 
          pd_t * opd, 
          char * file, 
          int    unique,
          globus_off_t off, 
          globus_off_t len)
{
	uh_t    * uh  = NULL;
	errcode_t ec  = EC_SUCCESS;
	char    * filename = NULL;
	int       ext   = 0;
	int       fds[2];
	int       flags = 0;

	uh = pd->unixpriv = _unix_init(pd->unixpriv);

	if (*file == '|')
	{
		if (pipe(fds))
			return ec_create(EC_GSI_SUCCESS,
			                 EC_GSI_SUCCESS,
			                 "pipe failed: %s",
			                 strerror(errno));

		uh->pid = fork();
		if (uh->pid == -1)
		{
			close(fds[0]);
			close(fds[1]);
			return ec_create(EC_GSI_SUCCESS,
			                 EC_GSI_SUCCESS,
			                 "Failed to fork process: %s",
			                 strerror(errno));
		}

		if (!uh->pid)
		{
			for (file++; *file != '\0' && isspace(*file); file++);
			close(fds[1]);
			dup2(fds[0], 0);
			execl("/bin/sh", "sh", "-c", file, NULL);
			execlp("sh", "sh", "-c", file, NULL);
			exit (1);
		}

		close(fds[0]);
		uh->fd = fds[1];
		return EC_SUCCESS;
	}

	flags = O_CREAT|O_WRONLY;
	if (off == (globus_off_t)-1)
		flags |= O_TRUNC;

	if (unique)
		flags |= O_EXCL;

	filename = Strdup(file);
	do
	{
		uh->fd = open(filename, flags, S_IRUSR|S_IWUSR);
		if (uh->fd == -1 && unique && errno == EEXIST)
		{
			FREE(filename);
			filename = Sprintf(NULL, "%s.%d", file, ++ext);
		}
	} while (uh->fd == -1 && unique && ext < 1000000 && errno == EEXIST);
	FREE(filename);

	if (uh->fd == -1)
		ec = ec_create(EC_GSI_SUCCESS,
		               EC_GSI_SUCCESS,
		               "Failed to open %s: %s",
		               file,
		               strerror(errno));

	return ec;
}


static errcode_t
unix_read(pd_t          *  pd, 
          char          ** buf,
          globus_off_t  *  off,
          size_t        *  len,
          int           *  eof)
{
	uh_t      * uh   = (uh_t *) pd->unixpriv;
	errcode_t   ec   = EC_SUCCESS;

	*eof = 0;
	*off = uh->off;
	*buf = (char *) malloc(s_blocksize());

	*len = s_blocksize();
	if (uh->len != -1 && uh->len < s_blocksize())
		*len = (size_t)uh->len;

	*len = read(uh->fd, *buf, *len);

	if (*len > 0 && uh->len != -1)
		uh->len -= *len;

	if (*len > 0)
		uh->off += *len;

	if (*len == -1)
		ec = ec_create(EC_GSI_SUCCESS,
		               EC_GSI_SUCCESS,
		               "read error: %s",
		               strerror(errno));

	if (*len == 0 || uh->len == 0)
		*eof = 1;

	return ec;
}

static errcode_t 
unix_write(pd_t * pd, 
           char          * buf, 
           globus_off_t    off, 
           size_t          len,
           int             eof)

{
	uh_t * uh = (uh_t *) pd->unixpriv;
	ssize_t   cnt = 0;
	globus_off_t offset = 0;
	errcode_t ec  = EC_SUCCESS;

	offset = lseek(uh->fd, off, SEEK_SET);
	if (offset != off && errno != ESPIPE)
	{
		FREE(buf);
		if (offset == (globus_off_t)-1)
			return ec_create(EC_GSI_SUCCESS,
			                 EC_GSI_SUCCESS,
			                 "seek() failed: %s",
			                 strerror(errno));
		else
			return ec_create(EC_GSI_SUCCESS,
			                 EC_GSI_SUCCESS,
			                 "seek() failed for unknown reason.");
	}

	while (len > 0 && ec == EC_SUCCESS)
	{
		cnt = write(uh->fd, buf + cnt, len - cnt);
		if (cnt == -1)
			ec = ec_create(EC_GSI_SUCCESS,
			               EC_GSI_SUCCESS,
			               "write failed: %s",
			               strerror(errno));

		len -= cnt;
	}

	FREE(buf);
	return ec;
}

static errcode_t
unix_close(pd_t * pd)
{
	uh_t * uh = (uh_t *) pd->unixpriv;
	int stat_loc = 0;
	pid_t pid = 0;

	if (!uh)
		return EC_SUCCESS;

	if (uh->fd != -1)
		close(uh->fd);
	uh->fd = -1;

	if (uh->pid > 0)
		waitpid(pid, &stat_loc, 0);
	uh->pid = 0;

	uh->off = 0;
	uh->len = 0;

	return EC_SUCCESS;
}


static errcode_t
unix_list(pd_t * pd, char * path)
{
	int fds[2];
	uh_t * uh  = NULL;

	uh = pd->unixpriv = _unix_init(pd->unixpriv);
	uh->len = -1;

	if (pipe(fds))
		return ec_create(EC_GSI_SUCCESS,
		                 EC_GSI_SUCCESS,
		                 "pipe failed: %s",
		                 strerror(errno));

	uh->pid = fork();
	if (uh->pid == -1)
	{
		close(fds[0]);
		close(fds[1]);
		return ec_create(EC_GSI_SUCCESS,
		                 EC_GSI_SUCCESS,
		                 "Failed to fork process: %s",
		                 strerror(errno));
	}

	if (!uh->pid)
	{
		close(fds[0]);
		dup2(fds[1], 1);
		dup2(fds[1], 2);
		execl("/bin/ls", "/bin/ls", "-la", path, NULL);
		execlp("ls", "ls", "-la", path, NULL);
		exit (1);
	}

	close(fds[1]);
	uh->fd = fds[0];
	return EC_SUCCESS;
}

static errcode_t
unix_pwd(pd_t * pd, char ** path)
{
	char   * cptr = NULL;
	size_t   len  = 0;

	*path = NULL;

	do
	{
		len  += 1024;
		*path = (char *) realloc(*path, len);
		cptr  = getcwd(*path, len);
	} while (!cptr && errno == ERANGE);

	if (!cptr)
	{
		return ec_create(EC_GSI_SUCCESS,
		                 EC_GSI_SUCCESS,
		                 "getcwd failed: %s", strerror(errno));
		FREE(*path);
		*path = NULL;
	}

	return EC_SUCCESS;
}

static errcode_t
unix_chdir(pd_t * pd, char * path)
{
	int    ret   = 0;
	int    len   = 0;
	uh_t * uh    = (uh_t *)pd->unixpriv;
	char * cwd   = NULL;
	char * buf   = NULL;
	errcode_t ec = EC_SUCCESS;

	uh = pd->unixpriv = _unix_init(uh);

	if (strcmp(path, "-") == 0)
	{
		if (!uh->opwd)
		{
			return ec_create(EC_GSI_SUCCESS,
			                 EC_GSI_SUCCESS,
			                 "OLDPWD not set.");
		}
		path = uh->opwd;
	}

	do
	{
		len  += 1024;
		buf  = (char *) realloc(buf, len);
		cwd  = getcwd(buf, len);
	} while (!cwd && errno == ERANGE);

	ret = chdir(path);
	if (ret)
		ec = ec_create(EC_GSI_SUCCESS,
		               EC_GSI_SUCCESS,
		               "chdir failed: %s", strerror(errno));

	if (!ret)
	{
		FREE(uh->opwd);
		uh->opwd = cwd;
	}

	if (!cwd)
		FREE(buf);
	return ec;
}

static errcode_t
unix_chgrp(pd_t * pd, char * group, char * path)
{
	int    ret = 0;
	int    len = 0;
	char * buf = NULL;
	struct group  * grpp  = NULL;
	struct group    grp;
	errcode_t ec = EC_SUCCESS;

	if (!IsInt(group))
	{
		do {
			len += 128;
			buf = (char *) realloc(buf, len);

			ret = getgrnam_r(group,
			                &grp,
			                 buf,
			                 len,
			                &grpp);
		} while (ret == ERANGE);

		if (ret)
		{
			ec = ec_create(EC_GSI_SUCCESS,
			               EC_GSI_SUCCESS,
			               "Failed to map %s to a gid: %s",
			               group,
			               strerror(ret));
			goto cleanup;
		}

		if (!grpp)
		{
			ec = ec_create(EC_GSI_SUCCESS,
			               EC_GSI_SUCCESS,
			               "No such group: %s",
			               group);
			goto cleanup;
		}
		ret = chown(path, (uid_t)-1, grp.gr_gid);
	} else
	{
		ret = chown(path, (uid_t)-1, atoi(group));
	}

	if (ret)
		ec = ec_create(EC_GSI_SUCCESS,
		               EC_GSI_SUCCESS,
		               "chgrp failed: %s",
		               strerror(errno));

cleanup:
	FREE(buf);
	return ec;
}

static errcode_t
unix_chmod(pd_t * pd, int perms, char * path)
{
	int ret = 0;

	ret = chmod(path, perms);

	if (ret)
		return ec_create(EC_GSI_SUCCESS,
		                 EC_GSI_SUCCESS,
		                 "chmod failed: %s", strerror(errno));

	return EC_SUCCESS;
}

static errcode_t
unix_mkdir(pd_t * pd, char * path)
{
	int ret = 0;

	ret = mkdir(path, 0700);

	if (ret && errno != EEXIST)
		return ec_create(EC_GSI_SUCCESS,
		                 EC_GSI_SUCCESS,
		                 "mkdir failed: %s", strerror(errno));

	return EC_SUCCESS;
}

static errcode_t
unix_rename(pd_t * pd, char * old, char * new)
{
	int ret = 0;

	ret = rename(old, new);

	if (ret)
		return ec_create(EC_GSI_SUCCESS,
		                 EC_GSI_SUCCESS,
		                 "rename failed: %s", strerror(errno));

	return EC_SUCCESS;
}

static errcode_t
unix_rm(pd_t * pd, char * path)
{
	int ret = 0;

	ret = unlink(path);

	if (ret)
		return ec_create(EC_GSI_SUCCESS,
		                 EC_GSI_SUCCESS,
		                 "unlink failed: %s", strerror(errno));

	return EC_SUCCESS;
}

static errcode_t
unix_rmdir(pd_t * pd, char * path)
{
	int ret = 0;

	ret = rmdir(path);

	if (ret)
		return ec_create(EC_GSI_SUCCESS,
		                 EC_GSI_SUCCESS,
		                 "rmdir failed: %s", strerror(errno));

	return EC_SUCCESS;
}

static errcode_t
unix_quote(pd_t * pd, char * cmd, char ** resp)
{
	*resp = NULL;
	return ec_create(EC_GSI_SUCCESS,
	                 EC_GSI_SUCCESS,
	                 "quote not supported locally");
}

static void
unix_mlsx_feats(pd_t * pd, mf_t * mf)
{
	mf->Type=1;
	mf->Size=1;
	mf->Modify=1;
	mf->Perm=1;
	mf->Charset=1;
	mf->UNIX_mode=1;
	mf->UNIX_owner=1;
	mf->UNIX_group=1;
	mf->Unique=1;
	mf->UNIX_slink=1;
}


static errcode_t
unix_stat(pd_t * pd, char * path, ml_t ** mlp)
{
	errcode_t ec  = EC_SUCCESS;
	char          * ppath = NULL;

	ppath = ParentPath(path);
	ec = _unix_mlsx(ppath, path, mlp);
	FREE(ppath);

	if (!ec && *mlp)
		(*mlp)->name = Strdup(path);

	return ec;
}

/*
 * Token is provided to this layer for performance. Mostly useful in
 * the FTP code.
 */
static errcode_t
unix_readdir(pd_t * pd, char * ppath, ml_t *** mlp, char * token)
{
	int   index = 0;
	int   ret   = 0;
	DIR  * dirp = NULL;
	char * path = NULL;
	struct dirent * entry  = NULL;
	struct dirent * result = NULL;
	errcode_t ec    = EC_SUCCESS;
	struct stat stbuf;
	long namemax = 0;

	if (ppath == NULL)
		ppath = ".";

	ret = stat(ppath, &stbuf);
	if (ret)
		return ec_create(EC_GSI_SUCCESS,
		                 EC_GSI_SUCCESS,
		                 "%s: %s",
		                 ppath,
		                 strerror(errno));

	if (!S_ISDIR(stbuf.st_mode))
		return ec_create(EC_GSI_SUCCESS,
		                 EC_GSI_SUCCESS,
		                 "%s: Not a directory.",
		                 ppath);

	namemax = pathconf(ppath, _PC_NAME_MAX);
	if (namemax == -1)
		return ec_create(EC_GSI_SUCCESS,
		                 EC_GSI_SUCCESS,
		                 "Could not determine NAME_MAX: %s",
		                 strerror(errno));

	dirp = opendir(ppath);
	if (dirp == NULL)
		return ec_create(EC_GSI_SUCCESS,
		                 EC_GSI_SUCCESS,
		                 "%s: %s",
		                 ppath,
		                 strerror(errno));

	entry = (struct dirent *) malloc(namemax + 1 + sizeof(struct dirent));

	while ((ret = readdir_r(dirp, entry, &result)) == 0 && result != NULL)
	{
		/* Allow '.' and '..' to the upper layer. */

		if (token)
		{
			/* Regular expression match. */
			if (s_glob() && fnmatch(token, entry->d_name, 0))
				continue;

			/* Literal match. */
			if (!s_glob() && strcmp(token, entry->d_name) == 0)
				continue;
		}

		*mlp = (ml_t **) realloc(*mlp, (index+2)*sizeof(ml_t*));
		(*mlp)[index+1] = NULL;

		path = Sprintf(NULL, "%s/%s", ppath, entry->d_name);
		ec   = _unix_mlsx(ppath, path, &((*mlp)[index]));
		FREE(path);

		if (ec)
			goto cleanup;

		if ((*mlp)[index])
		{
			(*mlp)[index]->name = Strdup(entry->d_name);
			index++;
		}
	}

	if (ret)
		ec = ec_create(EC_GSI_SUCCESS,
		               EC_GSI_SUCCESS,
		               "%s: %s",
		               ppath,
		               strerror(errno));

cleanup:
	FREE(entry);
	closedir(dirp);

	if (ec)
	{
		for (index = 0; *mlp && (*mlp)[index]; index++)
		{
			ml_delete((*mlp)[index]);
		}

		FREE(*mlp);
		*mlp = NULL;
	}

	return ec;
}

static errcode_t
unix_size(pd_t * pd, char * path, globus_off_t * size)
{
	struct stat statbuf;
	int rval = 0;

	*size = 0;

	rval = stat(path, &statbuf);

	if (rval != 0)
		return ec_create(EC_GSI_SUCCESS,
		                 EC_GSI_SUCCESS,
		                 "%s: %s",
		                 path,
		                 strerror(errno));

	*size = statbuf.st_size;
	return EC_SUCCESS;
}

static errcode_t 
unix_expand_tilde (pd_t * pd, char * tilde, char ** fullpath)
{
	int    len  = 0;
	int    ret  = 0;
	char * buf  = NULL;
	char * path = NULL;
	char * user = NULL;
	char * tok  = NULL;
	struct passwd   pwd;
	struct passwd * pwdp  = NULL;
	errcode_t ec = EC_SUCCESS;

	*fullpath = NULL;

	if (!tilde || *tilde != '~')
	{
		*fullpath = Strdup(tilde);
		return ec;
	}

	tok = PathTok(tilde, 0);

	if (strcmp(tok, "~") == 0)
		path = getenv("HOME");

	if (path == NULL)
	{
		user = tok + 1;
		if (*user == '\0')
		{
			do {
				len += 128;
				buf = (char *) realloc(buf, len);

				ret = getpwuid_r(geteuid(),
				                &pwd,
				                 buf,
				                 len,
				                &pwdp);
			} while (ret == ERANGE);
		} else
		{
			do {
				len += 128;
				buf = (char *) realloc(buf, len);

				ret = getpwnam_r(user,
				                &pwd,
				                 buf,
				                 len,
				                &pwdp);
			} while (ret == ERANGE);
		}

		if (!ret && pwdp)
			path = pwd.pw_dir;
	}

	if (path)
		*fullpath = MakePath(path, tilde + strlen(tok));

	FREE(tok);
	FREE(buf);

	if (!*fullpath)
		ec = ec_create(EC_GSI_SUCCESS,
		               EC_GSI_SUCCESS,
		               "Unable to expand %s.",
		               tilde);

	return ec;
}

static errcode_t 
unix_stage (pd_t * pd, char * path, int * staged)
{
	*staged = 1;
	return EC_SUCCESS;
}

errcode_t
unix_cksum (pd_t * pd, char * file, int * supported, unsigned int * crc)
{
	errcode_t ec = EC_SUCCESS;
	ck_t * ckp = NULL;
	FILE * fp  = NULL;
	char * buf = NULL;
	size_t len = 0;

	*supported = 1;

	fp = fopen(file, "r");
	if (!fp)
		return ec_create(EC_GSI_SUCCESS,
		                 EC_GSI_SUCCESS,
		                 "Failed to open file for summing: %s",
		                 strerror(errno));

	cksum_init(&ckp);
	buf = (char *) malloc(s_blocksize());

	while (1)
	{
		len = fread(buf, 1, s_blocksize(), fp);
		if (ferror(fp))
		{
			ec = ec_create(EC_GSI_SUCCESS,
			               EC_GSI_SUCCESS,
			               "Failed to read file for summing: %s",
			               strerror(errno));
			break;
		}

		if (len == 0)
			break;

		cksum_calc(ckp, buf, len);
		if (len != s_blocksize())
			break;
	}

	fclose(fp);
	FREE(buf);
	*crc = cksum_sum(ckp);
	cksum_destroy(ckp);
	return ec;
}

#ifdef SYSLOG_PERF
char *
unix_rhost (pd_t * pd)
{
	return NULL;
}
#endif /* SYSLOG_PERF */

const Linterface_t UnixInterface = {
	NULL, /* Connect */
	unix_connected,
	unix_disconnect,
	unix_retr,
	unix_stor,
	NULL, /* appefile */
	unix_read,
	unix_write,
	unix_close,
	unix_list,
	unix_pwd,
	unix_chdir,
	unix_chgrp,
	unix_chmod,
	unix_mkdir,
	unix_rename,
	unix_rm,
	unix_rmdir,
	unix_quote,
	unix_mlsx_feats,
	unix_stat,
	unix_readdir,
	unix_size,
	unix_expand_tilde,
	unix_stage,
	unix_cksum,
#ifdef SYSLOG_PERF
	unix_rhost,
#endif /* SYSLOG_PERF */
};

static errcode_t
_unix_mlsx(char * ppath, char * path, ml_t ** mlp)
{
	errcode_t ec  = EC_SUCCESS;
	int    ret          = 0;
	int    len          = 0;
	int    or           = 0;
	int    ow           = 0;
	int    ox           = 0;
	int    por          = 0;
	int    pow          = 0;
	int    pox          = 0;
	ml_t * ml           = NULL;
	char          * buf   = NULL;
	struct group  * grpp  = NULL;
	struct group    grp;
	struct passwd   pwd;
	struct passwd * pwdp  = NULL;
	struct stat     stbuf;

	*mlp = NULL;

	ret = stat(path, &stbuf);
	if (ret && errno == ENOENT)
		return ec;

	if (ret)
		return ec_create(EC_GSI_SUCCESS,
		                 EC_GSI_SUCCESS,
		                 "%s: %s",
		                 path,
		                 strerror(errno));

	ml = *mlp = (ml_t *) malloc(sizeof(ml_t));
	memset(ml, 0, sizeof(ml_t));
	ml->mf.Type       = 1;
	ml->mf.Size       = 1;
	ml->mf.Modify     = 1;
	ml->mf.Perm       = 1;
	ml->mf.UNIX_mode  = 1;
	ml->mf.UNIX_owner = 1;
	ml->mf.UNIX_group = 1;
	ml->mf.Unique     = 1;
	ml->type   = stbuf.st_mode & S_IFMT;
	ml->size   = stbuf.st_size;
	ml->modify = stbuf.st_mtime;

	or = !access(path, R_OK);
	ow = !access(path, W_OK);
	ox = !access(path, X_OK);

	por = !access(ppath, R_OK);
	pow = !access(ppath, W_OK);
	pox = !access(ppath, X_OK);

	ml->perms.appe     = S_ISREG(stbuf.st_mode) && ow;
	ml->perms.creat    = S_ISDIR(stbuf.st_mode) && ow && ox;
	ml->perms.exec     = S_ISDIR(stbuf.st_mode) && ox;
	ml->perms.delete   = pow && pox;
	ml->perms.rename   = pow && pox;
	ml->perms.list     = S_ISDIR(stbuf.st_mode) && or;
	ml->perms.mkdir    = S_ISDIR(stbuf.st_mode) && ow && ox;
	ml->perms.purge    = S_ISDIR(stbuf.st_mode) && ow && ox;
	ml->perms.retrieve = S_ISREG(stbuf.st_mode) && or;
	ml->perms.store    = S_ISREG(stbuf.st_mode) && ow;
	ml->UNIX_mode      = Sprintf(NULL, "%ol", stbuf.st_mode & 0x777);

	do {
		len += 128;
		buf = (char *) realloc(buf, len);

		ret = getpwuid_r(stbuf.st_uid, 
		                &pwd,
		                 buf,
		                 len,
		                 &pwdp);
	} while (ret == ERANGE);

	if (ret != 0 || pwdp == NULL)
		ml->UNIX_owner = Sprintf(NULL, "%d", stbuf.st_uid);
	else
		ml->UNIX_owner = Strdup(pwd.pw_name);

	do {
		len += 128;
		buf = (char *) realloc(buf, len);
		ret = getgrgid_r(stbuf.st_gid,
		                &grp,
		                 buf,
		                 len,
		                &grpp);
	} while (ret == ERANGE);

	if (ret != 0 || grpp == NULL)
		ml->UNIX_group = Sprintf(NULL, "%d", stbuf.st_gid);
	else
		ml->UNIX_group = Strdup(grp.gr_name);

	ml->unique = Sprintf(NULL, "%d", stbuf.st_ino);

	do {
		len += 128;
		buf = (char *) realloc(buf, len);
		ret = readlink(path, buf, len);
	} while ((ret == -1 && errno == ENAMETOOLONG) || ret == len);

	if (ret == -1 && errno != EINVAL)
		ec = ec_create(EC_GSI_SUCCESS,
		               EC_GSI_SUCCESS,
		               "readlink failed for %s: %s",
		               path,
		               strerror(errno));

	if (ret > -1)
	{
		buf[ret] = '\0';
		ml->UNIX_slink = Strdup(buf);
		ml->mf.UNIX_slink = 1;
	}

	FREE(buf);
	if (ec != EC_SUCCESS)
	{
		ml_delete(ml);
		*mlp = NULL;
	}

	return ec;
}

static uh_t *
_unix_init(uh_t * uh)
{
	if (!uh)
	{
		uh = (uh_t *) malloc(sizeof(uh_t));
		memset(uh, 0, sizeof(uh_t));
		uh->fd = -1;
	}
	return uh;
}

#ifdef NOT
static void
_unix_destroy(pd_t * pd)
{
	uh_t * uh = (uh_t *)pd->unixpriv;

	if (!uh)
		return;

	if (uh->fd != -1)
		close(uh->fd);
	FREE(uh->opwd);
	FREE(uh);
	pd->unixpriv = NULL;
}
#endif /* NOT */
