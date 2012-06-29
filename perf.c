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
#ifdef SYSLOG_PERF

#include <sys/socket.h>
#include <sys/vfs.h>
#include <stdlib.h>
#include <mntent.h>
#include <stdarg.h>
#include <syslog.h>
#include <string.h>
#include <stdio.h>
#include <netdb.h>
#include <globus_common.h>

#include "logical.h"
#include "misc.h"

/*
 * record by default.
 */
int
should_record(char * file)
{
	int    rc   = 0;
	int    dmi  = 1;
	struct statfs fsstatbuf;
	struct statfs fstatbuf;
	FILE * fp   = NULL;
	struct stat buf;
	struct mntent * mntent = NULL;

	rc = stat("/etc/mtab", &buf);
	if (rc)
		return dmi;

	rc = statfs(file, &fstatbuf);
	if (rc)
		return dmi;

	fp = setmntent("/etc/mtab", "r");
	if (!fp)
		return dmi;

	while ((mntent = getmntent(fp)) != NULL)
	{
		rc = statfs(mntent->mnt_dir, &fsstatbuf);
		if (rc)
			continue;

		if (memcmp(&fstatbuf.f_fsid, &fsstatbuf.f_fsid, sizeof(fsid_t)) == 0)
		{
			dmi = 0;
			if (strcmp(mntent->mnt_opts, "dmapi") == 0)
				dmi = 1;
			if (strncmp(mntent->mnt_opts, "dmapi,", 6) == 0)
				dmi = 1;
			if (strstr(mntent->mnt_opts, ",dmapi,"))
				dmi = 1;
			break;
		}
	}
	endmntent(fp);
	return dmi;
}

/*
 * realpath() w/ twice the length limit and w/o symbolic link dereferencing.
 */
char *
real_path(char * path)
{
	char   cbuf[2048];
	char * rpath = NULL;
	char * cwd   = NULL;
	char * cptr  = NULL;
	char * sptr  = NULL;

	if (*path == '/')
	{
		rpath = strdup(path);
	} else
	{
		/* Should never happen. */
		cwd = getcwd(cbuf, sizeof(cbuf));
		rpath = (char*) malloc(strlen(path) + strlen(cwd) + 2);
		sprintf(rpath, "%s/%s", cwd, path);
	}

	/* Remove '/./' */
	while ((cptr = strstr(rpath, "/./")))
	{
		memmove(cptr + 1, cptr + 3, strlen(cptr) - 2);
	}

	/* Remove '//' */
	while ((cptr = strstr(rpath, "//")))
	{
		memmove(cptr + 1, cptr + 2, strlen(cptr) - 1);
	}

	/* Remove '/../'*/
	while ((cptr = strstr(rpath, "/../")))
	{
		if (cptr == rpath)
		{
			memmove(cptr + 1, cptr + 4, strlen(cptr) - 3);
			continue;
		}

		for (sptr = --cptr; *sptr != '/'; sptr--);
		memmove(sptr + 1, cptr + 4, strlen(cptr) - 3);
	}

	/* Remove '/..$'*/
	cptr = rpath + strlen(rpath) - 1;
	if (*cptr == '.')
	{
		cptr--;
		if (*cptr == '.')
		{
			cptr--;
			if (*cptr == '/')
			{
				if (cptr == rpath)
				{
					*(cptr + 1) = '\0';
				} else
				{
					for (sptr = --cptr; *cptr != '/'; cptr--);
					*(sptr + 1) = '\0';
				}
			}
		}
	}
	return rpath;
}

char *
get_fqdn(char * host)
{
	int rc = 0;
	char name[NI_MAXHOST];
	struct addrinfo hints;
	struct addrinfo * res = NULL;

	if (!host)
		return NULL;

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_flags    = AI_ADDRCONFIG;
	hints.ai_family   = 0;
	hints.ai_socktype = 0;
	hints.ai_protocol = 0;

	rc = getaddrinfo(host, NULL, &hints, &res);

	if (rc)
		return Strdup(host);

	rc = getnameinfo(res->ai_addr,
	                 res->ai_addrlen,
	                 name,
	                 sizeof(name),
	                 NULL,
	                 0,
	                 0);

	freeaddrinfo(res);
	if (rc)
		return Strdup(host);

	return Strdup(name);
}

void
record_perf(lh_t         slh,
            lh_t         dlh,
            char       * spath,
            char       * dpath,
            globus_off_t len)
{
	char  * rpath = NULL;
	char  * rhost = NULL;
	char  * rfqdn = NULL;
	char  * way   = NULL;

	if (l_is_unix_service(slh))
	{
		if (!spath || strcmp(spath, "-") == 0 || *spath == '|')
			return;
		rpath = real_path(spath);
		rhost = l_rhost(dlh);
		rfqdn = get_fqdn(rhost);
		way   = "to:";
	}

	if (l_is_unix_service(dlh))
	{
		if (!dpath || strcmp(dpath, "-") == 0 || *dpath == '|')
			return;
		rpath = real_path(dpath);
		rhost = l_rhost(slh);
		rfqdn = get_fqdn(rhost);
		way   = "from:";
	}

	if (rpath && rfqdn && should_record(rpath))
	{
		openlog("uberftp", LOG_PID, LOG_USER);
		syslog(LOG_INFO, 
		       "user: %u  group: %u  file: %s  size: %ld %s %s",
		       getuid(),
		       getgid(),
		       rpath,
		       len,
		       way,
		       rfqdn);
		closelog();
	}

	Free(rpath);
	Free(rhost);
	Free(rfqdn);
}

#endif /* SYSLOG_PERF */
