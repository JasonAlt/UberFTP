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
#ifndef UBER_SETTINGS_H
#define UBER_SETTINGS_H

enum {
	ORDER_BY_NAME,
	ORDER_BY_SIZE,
	ORDER_BY_TYPE,
	ORDER_BY_NONE,
};

void s_init(void);
void s_setactive(void);
void s_setascii(void);
void s_setbinary(void);
void s_setblocksize(long long size);
void s_setcksum(int on);
void s_setcos(char * cos);
void s_setdebug(int lvl);
void s_setdcau(int lvl, char * subject);
void s_seteb(void);
void s_setfamily(char * family);
void s_setglob(int on);
void s_sethash(void);
void s_setkeepalive(int);
void s_setmlsx(int on);
void s_setorder(int);
void s_setparallel(int cnt);
void s_setpassive(void);
void s_setpbsz(long long length);
void s_setprot(int lvl);
void s_setresume(char * path);
void s_setretry(int cnt);
void s_setrunique(void);
void s_setstream(void);
void s_setsunique(void);
void s_settcpbuf(long long);
void s_setwait(void);

int    s_ascii(void);
long long s_blocksize(void);
int    s_cksum(void);
char * s_cos(void);
int    s_dcau(void);
char * s_dcau_subject(void);
int    s_debug(void);
int    s_debug_set(void);
char * s_family(void);
int    s_glob(void);
int    s_hash(void);
int    s_order(void);
int    s_keepalive(void);
unsigned short s_maxsrc(void);
unsigned short s_maxport(void);
unsigned short s_minsrc(void);
unsigned short s_minport(void);
int       s_mlsx(void);
int       s_parallel(void);
int       s_passive(void);
long long s_pbsz(void);
int       s_prot(void);
char    * s_resume(void);
int       s_retry(void);
int       s_runique(void);
int       s_stream(void);
int       s_sunique(void);
long long s_tcpbuf(void);
int       s_wait(void);

#endif /* UBER_SETTINGS_H */
