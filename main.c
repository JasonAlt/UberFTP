/*
 * University of Illinois/NCSA Open Source License
 *
 * Copyright © 2003-2012, NCSA.  All rights reserved.
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
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <termios.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

#include <globus_module.h>
#include <gssapi.h>

#include "config.h"
#include "settings.h"
#include "errcode.h"
#include "output.h"
#include "misc.h"
#include "cmds.h"
#include "gsi.h"

#ifdef DMALLOC
#include "dmalloc.h"
#endif /* DMALLOC */

#ifdef MSSFTP
 #define EXIT(x) exit(x & ~CMD_EXIT)
#else /* MSSFTP */
 #define EXIT(x) exit((x & ~CMD_EXIT) != 0)
#endif /* MSSFTP */


static void   _m_signals();
static char * _m_read_stdin();
static void   _m_parse_cmdline(int argc, char * argv[]);

static char * _m_grab_arg(char * argv[], char * cmd, int * i, int argcnt);

static int    _m_is_opt_arg(char * argv[], int * i);
static char * _m_grab_opt_arg(char * argv[], char * cmd, int * i, int argcnt);
static int    _m_check_options(char * argv[], int * i);
static char * _m_grab_dcau_arg(char * argv[], int * i);

static int    _m_is_cmd_arg(char * arg);
static char * _m_check_cmd(char *   argv[], 
                           char *   cmd, 
                           int  *   i,
                           char **  url);
static void _m_parse_cmd_args(int argc, char * argv[]);

static int _m_is_host_arg(char * arg);
static void _m_parse_host_args(int argc, char * argv[]);

static int  _m_is_url(char * str);
static int  _m_is_url_arg(char * arg);
static void _m_parse_url_args(int argc, char * argv[]);
static int  _m_parse_url(char        * url, 
                         char       ** host, 
                         char       ** port,
                         char       ** user,
                         char       ** pass,
                         char       ** path) ;
static void
_m_parse_url_file(char *  urlfile,
                  char ** srcurl,
                  char ** dsturl);

static cmdret_t _m_open(char * cmd, 
                        char * host, 
                        char * port, 
                        char * user, 
                        char * pass);


static char *  host     = NULL;
static char *  port     = NULL;
static char *  user     = NULL;
static char *  pass     = NULL;
static char *  urlfile  = NULL;
static char *  srcurl   = NULL;
static char *  dsturl   = NULL;
static char ** cmdlist  = NULL;
static int     cmdlen   = 0;
static char ** optlist  = NULL;
static int     optlen   = 0;

#ifndef CMDS2MAN
int
main(int argc, char * argv[])
{
	cmdret_t  cr = CMD_SUCCESS;
	char * input = NULL;
	char * shost, * dhost = NULL;
	char * sport, * dport = NULL;
	char * suser, * duser = NULL;
	char * spass, * dpass = NULL;
	char * spath, * dpath = NULL;
	int    rval  = 0;
	int    i     = 0;

#ifdef MSSFTP
	/* 
	 * Grab the system credentials while we can.
	 * If the binary is setuid root, this will remove all priviledge. 
	 * Otherwise, it will just set the euid to the ruid. Either way, if the
	 * cred acquire was successful, it should be saved for further use,
	 * IE DCAU and open calls.
	 */
	ec = gsi_init();
	setuid(getuid());

	if (ec != EC_SUCCESS)
	{
		ec_print(ec);
		exit (CMD_ERR_OTHER);
	}
#endif /* MSSFTP */

	_m_signals();
	cmd_init();
	s_init();

	globus_module_activate(GLOBUS_GSI_GSSAPI_MODULE);

	_m_parse_cmdline(argc, argv);

	for (i = 0; i < optlen; i++)
	{
		cr = cmd_intrptr(optlist[i]);
		if (cr != CMD_SUCCESS)
			EXIT(cr);
	}

#ifndef MSSFTP
	if (srcurl || urlfile)
	{
		do {
			if (urlfile)
				_m_parse_url_file(urlfile, &srcurl, &dsturl);
			if (!srcurl)
				exit(0);

			rval = _m_parse_url(srcurl,
			                   &shost,
			                   &sport,
			                   &suser,
			                   &spass,
			                   &spath);
			if (rval)
				exit(1);
	
			if (dsturl)
			{
				rval = _m_parse_url(dsturl,
				                   &dhost,
				                   &dport,
				                   &duser,
				                   &dpass,
				                   &dpath);
	
				if (rval)
					exit(1);
			}

			if (shost)
			{
				cr = _m_open(dhost ? "lopen" : "open", 
				             shost,
				             sport,
				             suser,
				             spass);
				if (cr != CMD_SUCCESS)
					EXIT(cr);
			}
	
	
			if (dhost)
			{
				cr = _m_open("open", 
				              dhost,
				              dport,
				              duser,
				              dpass);
				if (cr != CMD_SUCCESS)
					EXIT(cr);
			}
	
			input = Sprintf(NULL, cmdlist[0], spath, dpath);
			cr = cmd_intrptr(input);
			Free(input);
			if (cr != CMD_SUCCESS)
				EXIT(cr);
	
			if (shost && dhost)
				cr = cmd_intrptr("lclose");
			if (cr != CMD_SUCCESS)
				EXIT(cr);
			if (dhost || shost)
				cr = cmd_intrptr("close");
			if (cr != CMD_SUCCESS)
				EXIT(cr);
	
			FREE(dhost);
			FREE(shost);
			FREE(dpath);
			FREE(spath);
			FREE(dport);
			FREE(sport);
			FREE(duser);
			FREE(suser);
			FREE(dpass);
			FREE(spass);
			dhost = dpath = dport = duser = dpass = NULL;
			shost = spath = sport = suser = spass = NULL;

		} while (urlfile);
		EXIT(0);
	}
#endif /* !MSSFTP */

	if (!s_debug_set())
		s_setdebug(DEBUG_NORMAL);
	
#ifndef MSSFTP
	if (host)
#endif /* !MSSFTP */
	{
		cr = _m_open("open", host, port, user, pass);
		if (cr != CMD_SUCCESS)
			EXIT(cr);
	}

	for (i = 0; cr == CMD_SUCCESS && i < cmdlen; i++)
	{
		cr = cmd_intrptr(cmdlist[i]);
	}

	if (!srcurl && !urlfile && !cmdlist)
	{
		while (!(cr & CMD_EXIT) && (input = _m_read_stdin()))
		{
			cr |= cmd_intrptr(input);
		}
	}

	EXIT(cr);
}
#endif /* !CMDS2MAN */

static void
_m_usage()
{
printf(
#ifdef MSSFTP
       "Usage: mssftp [options] [cmds]\n"
#else /* MSSFTP */
       "Usage: uberftp [options] [host options] [host]\n"
       "       uberftp [options] [host options]  host cmds\n"
       "       uberftp [options] <srcurl> <dsturl>\n"
       "       uberftp [options] -f <urlfile>\n"
       "       uberftp [options] -cmd <url>\n"
       "\n"

  "Note: Only the first usage creates an interactive session.\n" 
#endif /* MSSFTP */
  "\n"
#ifndef MSSFTP
  "\thost      Connect to host.\n"
#endif /* !MSSFTP */
  "\tcmds      This specifies the FTP commands to run once the control\n"
  "\t          connection is establish. This list must be enclosed\n"
  "\t          in quotes. Multiple commands are semicolon delimited.\n"
  "\t          uberftp will execute these commands and then exit.\n"
#ifndef MSSFTP
  "\tsrcurl and dsturl\n"
  "\t          These denote the source URL and destination URL\n"
  "\t          respectively. The accepted forms are:\n"
  "\t              gsiftp://host[:port]/<path>\n"
  "\t              ftp://[user[:pass]@]host[:port]/<path>\n"
  "\t              file:<path>\n"
  "\turlfile\n"
  "\t          This file is a list of <srcurl> <dsturl> pairs, one pair \n"
  "\t          per line. Blanks lines and lines beginning with '#' are\n"
  "\t          ignored.\n"
  "\t-cmd <url>\n"
  "\t          This will execute the given command using the url as the\n"
  "\t          target. The supported commands and their syntax are listed\n"
  "\t          below.\n"
  "\n"

  "The \"host options\" are:\n"
  "\t-P port   Connect to port (default 2811 for GSI)\n"
  "\t-u user   Specify the user to authenticate as.\n"
  "\t-p pass | X\n"
  "\t          Use password 'pass' when authenticating with 'host'.\n"
  "\t          If 'pass' equals 'X', read the password from STDIN with\n"
  "\t          character echoing turned off.\n"
  "\n"
#endif /* !MSSFTP */

  "The \"options\" are:\n"
  "\t-active       Use ACTIVE mode for data transfers.\n"
  "\t-ascii        Use ASCII mode for data transfers.\n"
  "\t-binary       Use BINARY mode for data transfers.\n"
  "\t-blksize n    Set the internal buffer size to n.\n"
  "\t-cksum [on|off]\n"
  "\t              Enable/Disable CRC checks after file transfers.\n"
#ifdef MSSFTP
  "\t-d            Enable debugging. Same as '-debug 3'. Deprecated.\n"
#endif /* MSSFTP */
  "\t-debug   n    Set the debug level to n.\n"
  "\t-family  name Set the storage family to name.\n"
  "\t-cos     name Set the storage class of service to name.\n"
#ifdef MSSFTP
  "\t-g            Disable filename globbing. Same as '-glob off'. Deprecated.\n"
#endif /* MSSFTP */
  "\t-glob [on|off]\n"
  "\t              Enable/Disable filename globbing.\n"
  "\t-hash         Enable hashing.\n"
  "\t-keepalive n  Send control channel keepalive messages every n\n"
  "\t              seconds during data transfers.\n"
  "\t-mode  [E|S]  Switch the transfer mode to extend block (E) or\n"
  "\t              streams mode(S).\n"
  "\t-parallel n   Use n parallel data channels during extended block\n"
  "\t              transfers.\n"
  "\t-passive      Use PASSIVE mode for data transfers.\n"
  "\t-pbsz  n      Set the data protection buffer size to n bytes.\n"
  "\t-prot [C|S|E|P|]\n"
  "\t              Set the data protection level to clear (C),\n"
  "\t              safe (S), confidential (E) or private (P).\n"
  "\t-retry n      Retry commands that fail with transient errors n times.\n"
  "\t-resume path  Retry the recursive transfer starting at path.\n"
  "\t-tcpbuf n     Set the TCP read/write buffers to n bytes.\n"
  "\t-wait         This will cause the client to wait for remote files to\n"
  "\t              stage before attempting to transfer them.\n"
#ifdef MSSFTP
  "\t-v            Enable verbose mode. Same as '-debug 3'. Deprecated.\n"
#else /* MSSFTP */
  "\t-v            Print UberFTP version information and exit. Deprecated.\n"
#endif /* MSSFTP */
  "\t-version      Print UberFTP version information and exit.\n"
  "\t-versions     Print version information about all used globus modules\n"
  "\t              and exit.\n"
  "\n"

#ifndef MSSFTP
  "The supported \"-cmds\" are:\n"
  "\t-cat <url>\n"
  "\t              Print to stdout the contents of the remote file.\n"
  "\t-chgrp [-r] group <url>\n"
  "\t              Set the group ownership on the remote object(s).\n"
  "\t-chmod [-r] perms <url>\n"
  "\t              Set the permissions on the remote object(s).\n"
  "\t-dir [-r] <url>\n"
  "\t              List the contents of the remote object.\n"
  "\t-link <url> <path>\n"
  "\t              Create a hardlink to the remote object named <path>.\n"
  "\t-ls [-r] <url>\n"
  "\t              List the contents of the remote object.\n"
  "\t-mkdir <url>\n"
  "\t              Create the remote directory.\n"
  "\t-rename <url> <path>\n"
  "\t              Rename the remote object to the given <path>.\n"
  "\t-rm [-r] <url>\n"
  "\t              Remove the remote object(s).\n"
  "\t-rmdir <url>\n"
  "\t              Remove the remote directory.\n"
  "\t-size <url>\n"
  "\t              Return the size of the remote object.\n"
  "\t-stage -r seconds <url>\n"
  "\t              Attempt to stage the remote object(s) over the time\n"
  "\t              period given in seconds.\n"
  "\t-symlink <url> <path>\n"
  "\t              Create a symlink to the remote object named <path>.\n"
  "\n"
  "Note: uberftp uses passive STREAMS mode by default.\n"
#else /* !MSSFTP */
  "Note: mssftp uses active STREAMS mode by default.\n"
#endif /* !MSSFTP */
  "\n");

	exit(0);
}


static void
_m_signals()
{
	struct sigaction act;

	act.sa_handler   = SIG_IGN;
	act.sa_flags     = 0;
	act.sa_sigaction = NULL;
	sigemptyset(&act.sa_mask);

	sigaction(SIGPIPE, &act, NULL);

	act.sa_flags = SA_RESTART;
	sigaction(SIGINT, &act, NULL);
}

static char *
_m_read_stdin()
{
	int    off = 0;
	char * nl  = NULL;
	static int    eof    = 0;
	static int    buflen = 512;
	static char * buf    = NULL;

	if (!buf)
		buf = (char *) malloc(buflen);

	if (eof)
	{
		FREE(buf);
		return NULL;
	}

	printf("%s> ", PACKAGE_NAME);
	memset(buf, 0, buflen);

	while (1)
	{
		if(fgets(buf + off, buflen - off, stdin) == NULL)
		{
			eof = 1;
			break;
		}

		if ((nl = strrchr(buf, '\n')) != NULL)
			break;

		off = buflen -1;
		buflen += 512;
		buf = (char *) realloc(buf, buflen);
	}

	if (nl)
		*nl = '\0';

	return buf;

}

void
_m_parse_cmdline(int argc, char * argv[])
{
	int i = 0;

	for (i = 1; i < argc; i++)
	{
		if (_m_is_host_arg(argv[i]))
			return _m_parse_host_args(argc, argv);

#ifndef MSSFTP
		if (_m_is_url_arg(argv[i]))
			return _m_parse_url_args(argc, argv);
#endif /* !MSSFTP */

		if (_m_is_opt_arg(argv, &i))
			continue;

#ifndef MSSFTP
		if (_m_is_cmd_arg(argv[i]))
			return _m_parse_cmd_args(argc, argv);
#endif /* !MSSFTP */

		fprintf(stderr, "Illegal argument: %s\n", argv[i]);
		exit (1);
	}

	for (i = 1; i < argc; i++)
	{
		if (!_m_check_options(argv, &i))
		{
			fprintf(stderr, "Illegal option: %s.\n", argv[i]);
			exit (1);
		}
	}
}

static char *
_m_grab_arg(char * argv[], char * cmd, int * i, int argcnt)
{
	char * val = NULL;
	char * ret = NULL;

	if (strcmp(argv[*i], cmd) == 0)
	{
		for (; argcnt > 0; argcnt--)
		{
			val = argv[++(*i)];
			if (!val)
			{
				fprintf(stderr, "Missing arguments for %s.\n", cmd);
				exit (1);
			}
			if (*val == '-')
			{
				fprintf(stderr, "Illegal option for %s: %s.\n", cmd, val);
				exit (1);
			}
			ret = Sprintf(ret, 
			              "%s%s%s",
			              ret ? ret : "",
			              ret ? " " : "",
			              val);
		}
		return ret;
	}
	return NULL;
}

static int
_m_is_opt_arg(char * argv[], int * i)
{
	char * val = NULL;

	if ((val = _m_grab_opt_arg(argv, "-a",         i, 1))||
	    (val = _m_grab_opt_arg(argv, "-active",    i, 0))||
	    (val = _m_grab_opt_arg(argv, "-ascii",     i, 0))||
	    (val = _m_grab_opt_arg(argv, "-binary",    i, 0))||
	    (val = _m_grab_opt_arg(argv, "-blksize",   i, 1))||
	    (val = _m_grab_opt_arg(argv, "-cksum",     i, 1))||
	    (val = _m_grab_opt_arg(argv, "-debug",     i, 1))||
	    (val = _m_grab_opt_arg(argv, "-family",    i, 1))||
	    (val = _m_grab_opt_arg(argv, "-cos",       i, 1))||
#ifdef MSSFTP
	    (val = _m_grab_opt_arg(argv, "-i",         i, 0))||
	    (val = _m_grab_opt_arg(argv, "-d",         i, 0))||
	    (val = _m_grab_opt_arg(argv, "-g",         i, 0))||
#endif /* MSSFTP */
	    (val = _m_grab_opt_arg(argv, "-glob",      i, 1))||
	    (val = _m_grab_opt_arg(argv, "-hash",      i, 0))||
	    (val = _m_grab_opt_arg(argv, "-help",      i, 0))||
	    (val = _m_grab_opt_arg(argv, "-keepalive", i, 1))||
	    (val = _m_grab_opt_arg(argv, "-mode",      i, 1))||
	    (val = _m_grab_opt_arg(argv, "-parallel",  i, 1))||
	    (val = _m_grab_opt_arg(argv, "-passive",   i, 0))||
	    (val = _m_grab_opt_arg(argv, "-pbsz",      i, 1))||
	    (val = _m_grab_opt_arg(argv, "-prot",      i, 1))||
	    (val = _m_grab_opt_arg(argv, "-resume",    i, 1))||
	    (val = _m_grab_opt_arg(argv, "-retry",     i, 1))||
	    (val = _m_grab_opt_arg(argv, "-tcpbuf",    i, 1))||
	    (val = _m_grab_opt_arg(argv, "-wait",      i, 0))||
	    (val = _m_grab_opt_arg(argv, "-v",         i, 0))||
	    (val = _m_grab_opt_arg(argv, "-version",   i, 0))||
	    (val = _m_grab_opt_arg(argv, "-versions",  i, 0))||
	    (val = _m_grab_dcau_arg(argv, i)))
	{
		Free(val);
		return 1;
	}

	return 0;
}

static char *
_m_grab_opt_arg(char * argv[], char * cmd, int * i, int argcnt)
{
	char * val = NULL;
	char * ret = NULL;

	if (strcmp(argv[*i], cmd) == 0)
	{
		ret = Strdup(argv[*i]+1);
		for (; argcnt > 0; argcnt--)
		{
			val = argv[++(*i)];
			if (!val)
			{
				fprintf(stderr, "Missing arguments for %s.\n", cmd);
				exit (1);
			}
			if (*val == '-')
			{
				fprintf(stderr, "Illegal option for %s: %s.\n", cmd, val);
				exit (1);
			}
			ret = Sprintf(ret, "%s %s", ret, val);
		}
		return ret;
	}
	return NULL;
}

static int
_m_check_options(char * argv[], int * i)
{
	char * val = NULL;

	if ((val = _m_grab_opt_arg(argv, "-version", i, 0))
#ifndef MSSFTP
	   || (val = _m_grab_opt_arg(argv, "-v",       i, 0))
#endif /* MSSFTP */
       )
	{
		printf("%s %s\n", PACKAGE, PACKAGE_VERSION);
		exit (0);
	}

	if ((val = _m_grab_opt_arg(argv, "-versions", i, 0)))
	{
		globus_module_print_activated_versions(stdout, GLOBUS_TRUE);
		exit (0);
	}

	if ((val = _m_grab_opt_arg(argv, "-help", i, 0)) ||
	    (val = _m_grab_opt_arg(argv, "-?",    i, 0)))
	{
		_m_usage();
		exit (0);
	}

	if ((val = _m_grab_opt_arg(argv, "-a", i, 1)))
	{
		fprintf(stderr, "Warning: the -a option is deprecated, ignoring.\n");
		return 1;
	}

	if ((val = _m_grab_opt_arg(argv, "-active",    i, 0))||
	    (val = _m_grab_opt_arg(argv, "-ascii",     i, 0))||
	    (val = _m_grab_opt_arg(argv, "-binary",    i, 0))||
	    (val = _m_grab_opt_arg(argv, "-blksize",   i, 1))||
	    (val = _m_grab_opt_arg(argv, "-cksum",     i, 1))||
	    (val = _m_grab_opt_arg(argv, "-debug",     i, 1))||
	    (val = _m_grab_opt_arg(argv, "-family",    i, 1))||
	    (val = _m_grab_opt_arg(argv, "-cos",       i, 1))||
#ifdef MSSFTP
	    (val = _m_grab_opt_arg(argv, "-d",         i, 0))||
	    (val = _m_grab_opt_arg(argv, "-g",         i, 0))||
	    (val = _m_grab_opt_arg(argv, "-v",         i, 0))||
	    (val = _m_grab_opt_arg(argv, "-i",         i, 0))||
#endif /* MSSFTP */
	    (val = _m_grab_opt_arg(argv, "-glob",      i, 1))||
	    (val = _m_grab_opt_arg(argv, "-hash",      i, 0))||
	    (val = _m_grab_opt_arg(argv, "-keepalive", i, 1))||
	    (val = _m_grab_opt_arg(argv, "-mode",      i, 1))||
	    (val = _m_grab_opt_arg(argv, "-parallel",  i, 1))||
	    (val = _m_grab_opt_arg(argv, "-passive",   i, 0))||
	    (val = _m_grab_opt_arg(argv, "-pbsz",      i, 1))||
	    (val = _m_grab_opt_arg(argv, "-prot",      i, 1))||
	    (val = _m_grab_opt_arg(argv, "-resume",    i, 1))||
	    (val = _m_grab_opt_arg(argv, "-retry",     i, 1))||
	    (val = _m_grab_opt_arg(argv, "-tcpbuf",    i, 1))||
	    (val = _m_grab_opt_arg(argv, "-wait",      i, 0))||
	    (val = _m_grab_dcau_arg(argv, i)))
	{
		/* Special Cases for mssftp. */
		if (strcmp(val, "g") == 0)
		{
			fprintf(stderr,
			        "-g has been deprecated. Use '-glob off' instead.\n");
			free(val);
			val = strdup("glob off");
		}
		if (strcmp(val, "d") == 0 || strcmp(val, "v") == 0)
		{
			fprintf(stderr, 
			        "%s option has been deprecated. Use '-debug 3' instead.\n",
			        val);
			free(val);
			val = strdup("debug 3");
		}
		if (strcmp(val, "i") == 0)
		{
			fprintf(stderr, 
			        "-i option has been deprecated and has no effect.\n");
			return 1;
		}

		optlist = (char **) realloc(optlist, sizeof(char **)*(optlen+1));
		optlist[optlen++] = val;
		return 1;
	}

	return 0;
}

static char *
_m_grab_dcau_arg(char * argv[], int * i)
{
	char * val = NULL;
	char * ret = NULL;

	if (strcmp(argv[*i], "-dcau") == 0)
	{
		ret = Strdup(argv[*i]+1);
		val = argv[++(*i)];
		if (!val)
		{
			fprintf(stderr, "Missing arguments for dcau.\n");
			exit (1);
		}
		if (*val == '-')
		{
			fprintf(stderr, "Illegal option for dcau: %s.\n", val);
			exit (1);
		}
		ret = Strdup(val);

		if (strcasecmp(ret, "S") == 0)
		{
			val = argv[++(*i)];
			if (!val)
			{
				fprintf(stderr, "Missing arguments for dcau.\n");
				exit (1);
			}
			if (*val == '-')
			{
				fprintf(stderr, "Illegal option for dcau: %s.\n", val);
				exit (1);
			}
			ret = Sprintf(ret, "%s %s", ret, val);
		}

		return ret;
	}
	return NULL;
}

static int
_m_is_cmd_arg(char * arg)
{
	if (*arg != '-')
		return 0;

	if (strcmp(arg, "-cat")     == 0 ||
	    strcmp(arg, "-chgrp")   == 0 ||
	    strcmp(arg, "-chmod")   == 0 ||
	    strcmp(arg, "-dir")     == 0 ||
	    strcmp(arg, "-link")    == 0 ||
	    strcmp(arg, "-ls")      == 0 ||
	    strcmp(arg, "-mkdir")   == 0 ||
	    strcmp(arg, "-rename")  == 0 ||
	    strcmp(arg, "-rm")      == 0 ||
	    strcmp(arg, "-rmdir")   == 0 ||
	    strcmp(arg, "-size")    == 0 ||
	    strcmp(arg, "-symlink") == 0 ||
	    strcmp(arg, "-stage")   == 0)
	{
		return 1;
	}
	return 0;
}

static char *
_m_check_cmd(char *   argv[], 
             char *   cmd, 
             int  *   i,
             char **  url)
{
	char *  val    = NULL;
	char *  ret    = NULL;
	int     ind    = *i;

	*url = NULL;

	if (strcmp(argv[ind], cmd) == 0)
	{
		ret = Strdup(cmd + 1);
		for (ind++; (val = argv[ind]); ind++)
		{
			if (*val == '-' && strcmp(val, "-r"))
			{
				ind--;
				break;
			}

			if (_m_is_url(val))
			{
				if (*url)
				{
					fprintf(stderr, "Too many URLs for %s.\n", cmd);
					exit (1);
				}
				*url = val;
				ret  = Strcat(ret, " %s");
				continue;
			}

			ret = Sprintf(ret, "%s %s", ret, val);
		}
		if (!*url)
		{
			fprintf(stderr, "No URL given for %s.\n", cmd);
			exit (1);
		}
		*i = ind;
		return ret;
	}
	return NULL;
}

static void
_m_parse_cmd_args(int argc, char * argv[])
{
	int i   = 0;
	int ind = 0;
	char * url = NULL;
	char * cmd = NULL;

	for (i = 1; i < argc; i++)
	{
		if (_m_check_options(argv, &i))
			continue;

		ind   = i;
		if ((cmd = _m_check_cmd(argv, "-cat",     &i, &url)) ||
		    (cmd = _m_check_cmd(argv, "-chgrp",   &i, &url)) ||
		    (cmd = _m_check_cmd(argv, "-chmod",   &i, &url)) ||
		    (cmd = _m_check_cmd(argv, "-dir",     &i, &url)) ||
		    (cmd = _m_check_cmd(argv, "-link",    &i, &url)) ||
		    (cmd = _m_check_cmd(argv, "-ls",      &i, &url)) ||
		    (cmd = _m_check_cmd(argv, "-mkdir",   &i, &url)) ||
		    (cmd = _m_check_cmd(argv, "-rename",  &i, &url)) ||
		    (cmd = _m_check_cmd(argv, "-rm",      &i, &url)) ||
		    (cmd = _m_check_cmd(argv, "-rmdir",   &i, &url)) ||
		    (cmd = _m_check_cmd(argv, "-size",    &i, &url)) ||
		    (cmd = _m_check_cmd(argv, "-symlink", &i, &url)) ||
		    (cmd = _m_check_cmd(argv, "-stage",   &i, &url)))
		{
			if (cmdlist)
			{
				fprintf(stderr, "Only one command allowed.\n");
				exit (1);
			}

			srcurl  = url;
			cmdlist = (char **) malloc(sizeof(char*));
			cmdlist[0] = cmd;
			cmdlen = 1;
			continue;
		}

		fprintf(stderr, "Illegal option %s.\n", argv[i]);
		exit (1);
	}

	return;
}

static int
_m_is_host_arg(char * arg)
{
#ifndef MSSFTP
	if (strcmp(arg, "-P") == 0 ||
	    strcmp(arg, "-p") == 0 ||
	    strcmp(arg, "-u") == 0)
	{
		return 1;
	}
#endif /* !MSSFTP */

	/* Hostname ? */
	if (*arg != '-' && !_m_is_url(arg))
		return 1;

	return 0;
}

static void
_m_parse_host_args(int argc, char * argv[])
{
	int    i   = 0;
	char * val = NULL;

	for(i = 1; i < argc; i++)
	{
#ifndef MSSFTP
		if ((val = _m_grab_arg(argv, "-P", &i, 1)))
		{
			port = val;
			continue;
		}
		if ((val = _m_grab_arg(argv, "-u", &i, 1)))
		{
			user = val;
			continue;
		}
		if ((val = _m_grab_arg(argv, "-p", &i, 1)))
		{
			pass = val;
			continue;
		}
#endif /* !MSSFTP */

		if (_m_check_options(argv, &i))
			continue;

		switch (*(argv[i]))
		{
		case '-':
			fprintf(stderr, "Illegal option %s.\n", argv[i]);
			exit (1);
		default:
#ifndef MSSFTP
			if (!host)
			{
				host = argv[i];
				continue;
			}
#endif /* !MSSFTP */
			cmdlist = (char **)realloc(cmdlist, sizeof(char *)*(cmdlen+1));
			cmdlist[cmdlen++] = argv[i];
			continue;
		}
	}
}

static int
_m_is_url(char * str)
{
	if (str == NULL)
		return 0;

	if (strncmp(str, "gsiftp://", 9) == 0)
		return 1;

	if (strncmp(str, "ftp://", 6) == 0)
		return 1;

	if (strncmp(str, "file:", 5) == 0)
		return 1;

	return 0;
}

static int
_m_is_url_arg(char * arg)
{
	if (strcmp(arg, "-f") == 0)
		return 1;
	if (_m_is_url(arg))
		return 1;
	return 0;
}

static void
_m_parse_url_args(int argc, char * argv[])
{
	int i = 0;

	for (i = 1; i < argc; i++)
	{
		if (_m_check_options(argv, &i))
			continue;

		if (!srcurl && !urlfile)
		{
			if ((urlfile = _m_grab_arg(argv, "-f", &i, 1)))
				continue;
		}

		if (!_m_is_url(argv[i]) || urlfile)
		{
			fprintf(stderr, "Illegal option %s.\n", argv[i]);
			exit (1);
		}

		if (!srcurl)
			srcurl = argv[i];
		else if (!dsturl)
			dsturl = argv[i];
		else
		{
			fprintf(stderr, "Too many URLs on command line.\n");
			exit (1);
		}
	}

	if (!srcurl)
	{
		fprintf(stderr, "Missing source url\n");
		exit (1);
	}

	if (!dsturl)
	{
		fprintf(stderr, "Missing destination url\n");
		exit (1);
	}

	cmdlist = (char **) malloc(sizeof(char *));
	cmdlen  = 1;

	if (strncasecmp(dsturl, "file:", 5) == 0)
		cmdlist[0] = "get %s %s";
	else
		cmdlist[0] = "put %s %s";

	return;
}

/* 
 * 0  Successful parse.
 * 1  Failed parse.
 */
static int
_m_parse_url(char        * url, 
             char       ** host, 
             char       ** port,
             char       ** user,
             char       ** pass,
             char       ** path) 
{
	char * cptr = NULL;
	char * at   = NULL;
	int    gsiurl  = 0;
	int    ftpurl  = 0;

	*host = *port = *user = *pass = *path = NULL;

	if (url == NULL)
		return 1;

	if (strncasecmp(url, "file:", 5) == 0)
	{
		*path = Strdup(url + 5);
		return 0;
	}

	/* gsiftp://server.com:port/path */
	if (strncasecmp(url, "gsiftp://", 9) == 0)
	{
		gsiurl = 1;
		cptr   = url + 9;
	}

	/* ftp://user:pass@server.com:port/path */
	if (strncasecmp(url, "ftp://", 6) == 0)
	{
		ftpurl = 1;
		cptr   = url + 6;
	}

	if (!ftpurl && !gsiurl)
	{
		fprintf(stderr, "Illegal URL: %s\n", url);
		return 1;
	}

	*path = strchr(cptr, '/');
	if (!*path)
	{
		fprintf(stderr, "Missing path from the end of the url: %s\n", url);
		return 1;
	}

	at = Strnstr(cptr, "@", *path-cptr);
	if (at)
	{

		*user = Strndup(cptr, at - cptr);
		*pass = strchr(*user, ':');

		if (*pass)
		{
			if (!ftpurl)
			{
				fprintf(stderr, "pass not allowed for this URL:%s\n", url);
				Free(*user);
				return 1;
			}

			**pass = '\0';
			*pass  = Strdup((*pass)+1);
		}
		cptr = at + 1;
	}

	/* Host:Port */
	*host = Strndup(cptr, *path - cptr);
	*port = strchr(*host, ':');
	if (*port)
	{
		**port = '\0';
		*port  = Strdup((*port) + 1);
	}

	/* Adjust path for '~' */
	if (*((*path)+1) == '~')
		(*path)++;
	*path = Strdup(*path);

	if (*port == NULL)
	{
		if (gsiurl)
			*port = Strdup("2811");
		if (ftpurl)
			*port = Strdup("21");
	}

	return 0;
}

/*
 * 0 - success
 * 1 - done
 * 2 - error
 */
static void
_m_parse_url_file(char *  urlfile,
                  char ** srcurl,
                  char ** dsturl)
{
	static FILE * fptr   = NULL;
	static char * buf    = NULL;
	static int    buflen = 0;
	char        * spc    = NULL;
	int           offset = 0;
	int           eof    = 0;

	*srcurl = *dsturl = NULL;

	/* Make sure we have a buffer. */
	if (!buflen)
	{
		buflen = 1024;
		buf = (char *) malloc(buflen);
	}

	if (!fptr)
	{
		fptr = fopen(urlfile, "r");
		if (fptr == NULL)
		{
			fprintf(stderr, 
			        "Failed to open the URL file: %s\n", 
			        strerror(errno));
			exit(1);
		}
	}

	while (fgets(buf + offset, buflen - offset, fptr) != NULL)
	{
		eof = feof(fptr);
		if (!eof && buf[strlen(buf)-1] != '\n')
		{
			/* Didn't read the entire line */
			buflen += 1024;
			buf = (char *) realloc(buf, buflen);
			if (buf == NULL)
			{
				buflen = 0;
				fprintf(stderr, "Memory allocation error.\n");
				exit (1);
			}
			offset  = strlen(buf);
			continue;
		}

		/* Remove the newline. */
		if (buf[strlen(buf)-1] == '\n')
			buf[strlen(buf)-1] = '\0';

		/* Skip white space. */
		for (offset = 0; isspace(*(buf + offset)); offset++);

		/* Skip comments and blank lines. */
		if (*(buf + offset) == '\0' || *(buf + offset) == '#')
		{
			offset = 0;
			continue;
		}

		*srcurl = buf + offset;

		/* Now we should have a src/dst url pair. */
		for (spc = buf + offset; !isspace(*spc) && *spc != '\0'; spc++);
		if (*spc == '\0')
		{
			fprintf(stderr, "Missing destination URL: %s\n", buf + offset);
			exit (1);
		}

		*spc = '\0';

		for (spc++; isspace(*spc) && *spc != '\0'; spc++);
		if (*spc == '\0')
		{
			fprintf(stderr, "Missing desintation URL: %s\n", buf + offset);
			exit (1);
		}
		*dsturl = spc;

		for (; !isspace(*spc) && *spc != '\0'; spc++);
		*spc = '\0';

		return;
	}
}

cmdret_t
_m_open(char * cmd, char * host, char * port, char * user, char * pass)
{
	char * input = NULL;

	input = Sprintf(input, "%s", cmd);

#ifndef MSSFTP
	if (port)
		input = Sprintf(input, "%s -P %s", input, port);

	if (user)
		input = Sprintf(input, "%s -u %s", input, user);

	if (pass)
		input = Sprintf(input, "%s -p %s", input, pass);

	input = Sprintf(input, "%s %s", input, host);
#else /* !MSSFTP */
	input = Strdup("open");
#endif /* MSSFTP */

	return cmd_intrptr(input);
}



