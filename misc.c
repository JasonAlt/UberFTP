/*
 * University of Illinois/NCSA Open Source License
 *
 * Copyright © 2003-2014 NCSA.  All rights reserved.
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
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <netdb.h>
#include <ctype.h>
#include <time.h>

#include "config.h"
#include "misc.h"

#ifdef DMALLOC
#include "dmalloc.h"
#endif /* DMALLOC */


char *
GetRealHostName(char * host)
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
	                 NI_NAMEREQD);

	freeaddrinfo(res);
	if (rc)
		return Strdup(host);

	return Strdup(name);
}


char *
Strdup(char * str)
{
	if (str == NULL)
		return NULL;

	return strdup(str);
}

char *
Strcat(char * dst, char * src)
{
	if (dst == NULL)
		return Strdup(src);
	if (src == NULL)
		return dst;

	dst = (char *)realloc(dst, strlen(dst)+strlen(src)+1);
	return strcat(dst, src);
}

char *
Strncat(char * dst, char * src, int len)
{
	if (dst == NULL)
		return Strndup(src, len);
	if (src == NULL)
		return dst;

	dst = (char *)realloc(dst, strlen(dst)+len+1);
	dst[strlen(dst)+len] = '\0';
	memcpy(dst + strlen(dst), src, len);
	return dst;
}


char *
Sprintf(char * str, char * format, ...)
{
	int    ret  = 0;
	int    len  = 128;
	char * cptr = NULL;

	va_list ap;

	if (str)
		len = strlen(str) + 128;

	cptr = (char *) malloc(len * sizeof(char));
	while(1)
	{
		/* This will calculate the length. */
		va_start (ap, format);
		ret = vsnprintf(cptr, len, format, ap);
		va_end(ap);

		if (ret == -1)
			len += 128;
		else if (ret < len)
			break;
		else
			len = ret + 1;

		cptr = (char *) realloc(cptr, len);
	}

	FREE(str);

	return cptr;
}


void
Free(void * ptr)
{
	if (ptr)
		free(ptr);
}

int
Max(int x, int y)
{
	if (x < y)
		return y;
	return x;
}

int
Validperms(char * perms)
{
	int x = 0;

	if (strlen(perms) > 4)
		return 0;

	for (x = 0; x < strlen(perms); x++)
	{
		if (!Isoct(*(perms+x)))
			return 0;
	}

	return 1;
}

int
Isoct(char c)
{
	if (!isdigit(c))
		return 0;

	if ((int)c > (int)'7' || (int)c < (int)'0')
		return 0;

	return 1;
}

char *
DupQuotedString(char * src)
{
	char * fq  = NULL;
	char * lq  = NULL;
	char * ret = NULL;

	fq = strchr(src, '"');
	if (!fq)
		return NULL;

	lq = strchr(fq+1, '"');
	if (!lq)
		return NULL;

	*fq = '\0';
	*lq = '\0';

	ret = Strdup(fq+1);

	*fq = '"';
	*lq = '"';

	return ret;
}

char *
StrchrEsc(char *s, char * delim)
{
	int esc_next = 0;
	int esc_quote = 0;

	while (*s)
	{
		if (!esc_next)
		{
			if (*s == '\\')
			{
				s++;
				esc_next = 1;
				continue;
			}

			if (*s == '"')
			{
				s++;
				esc_quote = !esc_quote;
				continue;
			}

			if (strchr(delim, *s) && esc_quote == 0)
			{
				return s;
			}
		}
		else
		{
			esc_next = 0;
		}

		s++;
	}

	return NULL;
}

char *
StrtokEsc(char *s, int c, char **next)
{
	char *p, *bp;
	int esc_next = 0;
	int esc_quote = 0;

	*next = NULL;
	while (isspace(*s)) s++;

	bp = p = s;
	while (*p)
	{
		if (!esc_next)
		{
			if (*p == '\\')
			{
				p++;
				esc_next = 1;
				continue;
			}

			if (*p == '"')
			{
				p++;
				esc_quote = !esc_quote;
				continue;
			}

			if (*p == c && esc_quote == 0)
			{
				*bp = *p = '\0';
				*next = (p+1);
				return s;
			}
		}
		else
		{
			esc_next = 0;
		}

		*bp = *p;
		bp++;
		p++;
	}

	*bp = *p;
	if (p == s)
		return NULL;

	*next = p;
	return s;
}

char *
UnixPermStr(int mode)
{
	char * str = NULL;

	switch(mode & S_IFMT)
	{
	case S_IFDIR:
		str = Strdup("d");
		break;
	case S_IFREG:
		str = Strdup("-");
		break;
	case S_IFCHR:
		str = Strdup("c");
		break;
	case S_IFBLK:
		str = Strdup("b");
		break;
	case S_IFIFO:
		str = Strdup("p");
		break;
	case S_IFSOCK:
		str = Strdup("s");
		break;
	case S_IFLNK:
		str = Strdup("l");
		break;
	}

	str = Strcat(str, (mode & S_IRUSR) ? "r" : "-");
	str = Strcat(str, (mode & S_IWUSR) ? "w" : "-");
	if (mode & S_ISUID)
		str = Strcat(str, (mode & S_IXUSR) ? "s" : "S");
	else
		str = Strcat(str, (mode & S_IXUSR) ? "x" : "-");

	str = Strcat(str, (mode & S_IRGRP) ? "r" : "-");
	str = Strcat(str, (mode & S_IWGRP) ? "w" : "-");
	if (mode & S_ISGID)
		str = Strcat(str, (mode & S_IXGRP) ? "s" : "S");
	else
		str = Strcat(str, (mode & S_IXGRP) ? "x" : "-");

	str = Strcat(str, (mode & S_IROTH) ? "r" : "-");
	str = Strcat(str, (mode & S_IWOTH) ? "w" : "-");
	if (mode & S_ISVTX)
		str = Strcat(str, (mode & S_IXOTH) ? "t" : "T");
	else
		str = Strcat(str, (mode & S_IXOTH) ? "x" : "-");

	return str;
}

char *
ParentPath(char * path)
{
	char * ppath = NULL;
	char * delim;

	if ((delim = strrchr(path, '/')) == NULL)
		return Strdup("..");

	ppath = Strdup(path);
	delim = strrchr(ppath, '/');

	*(delim + 1) = '\0';
	return Strcat(ppath, "..");
}

int
IsGlob(char * regexp)
{
	if (!regexp)
		return 0;

	if (strchr(regexp, '*') == NULL &&
    	strchr(regexp, '?') == NULL &&
    	strchr(regexp, '[') == NULL)
	{
		return 0;
	}

	return 1;
}

char *
MakePath(char * dir, char * entry)
{
	if (!dir)
		return Strdup(entry);

	if (strcmp(dir, ".") == 0 && entry)
		return Strdup(entry);

	if (!entry)
		return Strdup(dir);

	if (strcmp(dir, "/") == 0)
		return Sprintf(Strdup(dir), "/%s", entry);
	return Sprintf(Strdup(dir), "%s/%s", dir, entry);
}

char *
PathTok(char * path, int depth)
{
	char * token = NULL;
	char * cptr  = NULL;
	char * scptr = NULL;

	if (*path == '|')
	{
		if (depth == 0)
			return Strdup(path);
		return NULL;
	}

	if (*path == '/' && depth == 0)
		return Strdup("/");

	if (*path == '/')
		depth--;

	cptr = scptr = Strdup(path);

	for (;(token = strtok(cptr, "/")) && depth; depth--, cptr = NULL);

	token = Strdup(token);
	FREE(scptr);
	return token;
}


time_t
ModFactToTime(char * modfact)
{
	time_t t   = 0;
	time_t now = 0;
	struct tm tm;
    struct tm ltz;
    struct tm gmt;

	memset(&tm, 0, sizeof(tm));

	/* epoc 00:00:00 UTC, January 1, 1970 */

	sscanf(modfact, 
	       "%4d%2d%2d%2d%2d%2d", 
	       &tm.tm_year, 
	       &tm.tm_mon,
	       &tm.tm_mday, 
	       &tm.tm_hour, 
	       &tm.tm_min, 
	       &tm.tm_sec);

	if (tm.tm_year < 1970)
		tm.tm_year = 1970;

	tm.tm_year -= 1900;
	tm.tm_mon  -= 1;

	t = mktime(&tm);
	if (t == (time_t)-1)
		t = 0;

	/* Adjust UTC to local timezone. */
	now = time(NULL);
	gmtime_r(&now, &gmt);
	localtime_r(&now, &ltz);

	if (t)
		t = t - (mktime(&gmt) - mktime(&ltz));

	return t;
}

char *
Strcasestr(char * str, char * pattern)
{
	int slen  = strlen(str);
	int plen  = strlen(pattern);
	int index = 0;

	for (; index <= (slen - plen); index++)
	{
		if (strncasecmp(str + index, pattern, plen) == 0)
			return str + index;
	}

	return NULL;
}

int
IsLongWithTag(char * str)
{
	int pos = 0;
	int len = 0;

	if (!str)
		return 0;

	len = strlen(str);

	for (pos = 0; pos < (len - 1); pos++)
	{
		if (!isdigit(str[pos]))
			return 0;
	}

	if (isdigit(str[pos]) ||
	   (toupper(str[pos]) == 'K') ||
	   (toupper(str[pos]) == 'M') ||
	   (toupper(str[pos]) == 'G') ||
	   (toupper(str[pos]) == 'T'))
	{
		return 1;
	}

	return 0;
}

long long
ConvLongWithTag(char * str)
{
	long long  value = 0;
	int        slen  = 0;
	int        c     = 0;

	if (!str)
		return 0;

	value = strtoll(str, NULL, 0);
	slen  = strlen(str);
	c     = toupper(str[slen-1]);

	if (c == 'K') return value * 1024;
	if (c == 'M') return value * 1024 * 1024;
	if (c == 'G') return value * 1024 * 1024 * 1024;
	if (c == 'T') return value * 1024 * 1024 * 1024 * 1024;

	return value;
}

int
IsInt(char * str)
{
	if (!str)
		return 0;

	for (; *str; str++)
	{
		if (!isdigit(*str))
			return 0;
	}

	return 1;
}

char *
Strnstr(char * s1, char * s2, int len)
{
	int i = 0;
	int sl = strlen(s2);

	if (!s1)
		return NULL;

	for(i = 0; i <= (len - sl); i++)
	{
		if (strncmp(s1+i, s2, sl) == 0)
			return s1+i;
	}
	return NULL;
}

char *
Strndup(char * src, int len)
{
	char * cptr = (char *) malloc(len + 1);
	memcpy(cptr, src, len);
	cptr[len] = '\0';
	return cptr;
}

char *
PathMinusRoot(char * path, char * root)
{
	char * tpath = Strdup(path);
	char * troot = Strdup(root);
	char * spath = tpath;
	char * sroot = troot;
	char * plast = NULL;
	char * rlast = NULL;
	char * psave = NULL;

	while ((psave = strtok_r(tpath, "/", &plast)))
	{
		tpath = NULL;
		if (!strtok_r(troot, "/", &rlast))
			break;
		troot = NULL;
	}

	if (psave)
		psave = path + (psave - spath);

	FREE(spath);
	FREE(sroot);

	return psave;
}

char *
Dirname(char * path)
{
    int    i      = 0;
    char * token  = NULL;
    char * ntoken = NULL;
    char * dir    = NULL;
    char * sdir   = NULL;

	if (!path)
		return NULL;

    token = PathTok(path, 0);

    for (i = 1; (ntoken = PathTok(path, i++)) != NULL; token = ntoken)
    {
        sdir = MakePath(dir, token);
        FREE(dir);
        dir  = sdir;
		FREE(token);
    }

	FREE(token);
    if (!dir && *path == '/')
        return Strdup("/");

    if (!dir)
        return Strdup(".");

    return dir;
}


char *
Basename(char * path)
{
    int    i      = 0;
    char * token  = NULL;
    char * ntoken = NULL;

	if (!path)
		return NULL;

    token = PathTok(path, 0);
    for (i = 1; (ntoken = PathTok(path, i++)) != NULL; token = ntoken)
    {
		FREE(token);
    }
	return token;
}

int
timeval_subtract (result, x, y)
          struct timeval *result, *x, *y;
{
	/* Perform the carry for the later subtraction by updating y. */
	if (x->tv_usec < y->tv_usec)
	{
		int nsec = (y->tv_usec - x->tv_usec) / 1000000 + 1;
		y->tv_usec -= 1000000 * nsec;
		y->tv_sec += nsec;
	}

	if (x->tv_usec - y->tv_usec > 1000000)
	{
		int nsec = (x->tv_usec - y->tv_usec) / 1000000;
		y->tv_usec += 1000000 * nsec;
		y->tv_sec -= nsec;
	}
     
       /* Compute the time remaining to wait.
          tv_usec is certainly positive. */
	result->tv_sec = x->tv_sec - y->tv_sec;
	result->tv_usec = x->tv_usec - y->tv_usec;
     
       /* Return 1 if result is negative. */
	return x->tv_sec < y->tv_sec;
}

/* Assumes stop is greater than start. */
void
SubTime(struct timeval * result,
        struct timeval * start,
        struct timeval * stop)
{
	struct timeval t1 = *start;
	struct timeval t2 = *stop;

	if (t1.tv_usec > t2.tv_usec)
	{
		t2.tv_sec--;
		t2.tv_usec += 1000000;
	}
	result->tv_usec = t2.tv_usec - t1.tv_usec;
	result->tv_sec  = t2.tv_sec  - t1.tv_sec;
}

char *
Convtime(struct timeval * start, struct timeval * stop)
{
	char * cptr    = NULL;
	int    hours   = 0;
	int    minutes = 0;
	float  seconds = 0;
	struct timeval tdiff;

	SubTime(&tdiff, start, stop);
	hours   = tdiff.tv_sec/3600;
	minutes = (tdiff.tv_sec - (hours*3600))/60;
	seconds = ((tdiff.tv_sec - (hours*3600) - (minutes*60)));
	seconds += (float)tdiff.tv_usec / 1000000;

	if (hours)
		cptr = Sprintf(cptr, "%d Hours", hours);

	if (minutes)
		cptr = Sprintf(cptr,
		               "%s%s%d Minutes",
		               cptr ? cptr : "",
		               cptr ? " " : "",
		               minutes);

	cptr = Sprintf(cptr,
	               "%s%s%f Seconds",
	               cptr ? cptr : "",
	               cptr ? " " : "",
	               seconds);

	return cptr;
}

char *
MkRate(struct timeval * start, struct timeval * stop, globus_off_t bytes)
{
	double rate = 0;
	double tim  = 0;
	struct timeval tdiff;

	SubTime(&tdiff, start, stop);
	tim   = tdiff.tv_sec;
	tim  += ((double)(tdiff.tv_usec) / (double)1000000);

	if (tim <= 0)
		return NULL;

	rate = ((double)bytes)/tim;
	
	if (rate > (1024*1024*1024))
		return Sprintf(NULL,
		               "%.3lf GB/s",
		               ((float)rate)/((float)1024*1024*1024));

	if (rate > (1024*1024))
		return Sprintf(NULL,
		               "%.3lf MB/s",
		               ((float)rate)/((float)1024*1024));

	if (rate > (1024))
		return Sprintf(NULL,
		               "%.3lf KB/s",
		               ((float)rate)/((float)1024));

	return Sprintf(NULL, "%.3lf B/s", rate);
}
