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
#ifndef UBER_MISC_H
#define UBER_MISC_H

#include "config.h"

#include <globus_common.h>

#include <stdarg.h>
#include <sys/time.h>
#include <time.h>

#ifdef HAVE_STRTOLL
  #define Strtoll strtoll
#else /* HAVE_STRTOLL */
  #define Strtoll strtol
#endif /* HAVE_STRTOLL */

#define FREE(x) {Free(x);(x)=NULL;}


char *
GetRealHostName(char * host);

char *
Strdup(char * str);

char *
Strcat(char * dst, char * src);

char *
Strncat(char * dst, char * src, int len);

char *
Sprintf(char * str, char * format, ...);

void
Free(void *);

int
Max(int x, int y);

int
Validperms(char * perms);

int
Isoct(char c);

char *
DupQuotedString(char * src);

char *
StrchrEsc(char *s, char * delim);

char *
StrtokEsc(char *s, int c, char **next);

char *
UnixPermStr(int mode);

char *
ParentPath(char * path);

int
IsGlob(char * regexp);

char *
MakePath(char * dir, char * entry);

char *
PathTok(char * path, int depth);

time_t
ModFactToTime(char * modfact);

char *
Strcasestr(char * str, char * pattern);

int
IsInt(char * str);

char *
Strnstr(char * s1, char * s2, int len);

char *
Strndup(char * src, int len);

char *
PathMinusRoot(char * path, char * root);

char *
Dirname(char * path);

char * 
Basename(char * path);

char *
Convtime(struct timeval * start, struct timeval * stop);

char *
MkRate(struct timeval * start, struct timeval * stop, globus_off_t size);

#endif /* UBER_MISC_H */
