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
#include "config.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <termios.h>
#include <string.h>
#include <dirent.h>
#include <libgen.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <time.h>
#include <pwd.h>

#include "linterface.h"
#include "filetree.h"
#include "settings.h"
#include "logical.h"
#include "output.h"
#include "cmds.h"
#include "misc.h"
#include "unix.h"
#include "ml.h"
#include "nc.h"

#ifdef SYSLOG_PERF
#include "perf.h"
#endif /* SYSLOG_PERF */

#ifdef DMALLOC
#include "dmalloc.h"
#endif /* DMALLOC */

#define C_RETRY(ec,cmd)         \
	{                           \
		int retcnt = s_retry(); \
		ec = EC_SUCCESS;        \
		do                      \
		{                       \
			ec_destroy(ec);     \
			ec = cmd;           \
			if (!ec_retry(ec))  \
				break;          \
		} while (retcnt-- > 0 || !s_retry()); \
	}


#define C_A_CMD         0x00
#define C_A_NOARGS      0x01
#define C_A_OINT        0x02 /* Optional 32bit integer. */
#define C_A_STRINGS     0x03 /* Expect 1 string, then optional strings. */
#define C_A_OSTRINGS    0x04 /* Optional strings. */
#define C_A_STRING      0x05 /* Expect 1 string.  */
#define C_A_2STRINGS    0x06 /* Expect 2 strings. */
#define C_A_STRINGPLUS  0x07 /* Expect 1 string, possibly 2. */
#define C_A_OSTRING     0x08 /* Possibly 1 string. */
#define C_A_OCHAR       0x09
#define C_A_2OFF        0x0A
#define C_A_OFF         0x0B
#define C_A_PERMS       0x0C
#define C_A_INT_STRINGS 0x0D /* Int followed by 1 or more strings. */
#define C_A_OCH_OSTRING 0x0E
#define C_A_2STRINGPLUS 0x0F /* Expect 2 strings, possibly more. */
#define C_A_2OSTRINGS   0x10 /* Possibly 2 strings. */
#define C_A_OLONG       0x11 /* Optional 64bit integer. */

#define C_A_OPT_P    0x020
#define C_A_OPT_p    0x040
#define C_A_OPT_u    0x080
#define C_A_OPT_r    0x100
#define C_A_OPT_d    0x200

#define C_A_RCH_1    0x1000
#define C_A_LCH_1    0x2000
#define C_A_RCH_2    0x4000
#define C_A_LCH_2    0x8000

#define C_A_OPT_MASK 0xFFE0

#define C_ISDIR(x) ((x) == 0 || (x) == S_IFDIR)
#define C_ISREG(x) ((x) == 0 || (x) == S_IFREG)

/* Connection Handle. */
typedef struct {
	lh_t lh;
} ch_t;

typedef struct cmd {
	void * func;
	char * name;
	int    ctype;
	char * desc;
	char * syntax;
	char * options;
} cmd_t;

ch_t glch; /* Local connection handle. */
ch_t grch; /* Remote connection handle. */

static cmdret_t  _c_lex(char * token);
static cmd_t   * _c_lookup(char * cmd);

static cmdret_t  _c_active();
static cmdret_t  _c_ascii();
static cmdret_t  _c_binary();
static cmdret_t  _c_blksize(long long size);
static cmdret_t  _c_bugs();
static cmdret_t  _c_cat(ch_t *, char ** files);
static cmdret_t  _c_cdup(ch_t *);
static cmdret_t  _c_chdir(ch_t *, char * path);
static cmdret_t  _c_chgrp(ch_t *, int rflag, char * group, char ** files);
static cmdret_t  _c_chmod(ch_t *, int rflag, int perms, char ** files);
static cmdret_t  _c_close(ch_t *);
static cmdret_t  _c_cksum(char * val);
static cmdret_t  _c_cos(char * cos);
static cmdret_t  _c_dcau(char mode, char * subject);
static cmdret_t  _c_debug(int lvl);
static cmdret_t  _c_glob(char * val);
static cmdret_t  _c_family(char * family);
static cmdret_t  _c_get(ch_t *, ch_t *, int, char *, char *);
static cmdret_t  _c_hash();
static cmdret_t  _c_help(char * cmd);
static cmdret_t  _c_keepalive(int seconds);
static cmdret_t  _c_list(ch_t *, int rflag, char * path, char * ofile);
static cmdret_t  _c_lscos(ch_t *);
static cmdret_t  _c_lsfam(ch_t *);
static cmdret_t  _c_mkdir(ch_t *, char ** dirs);
static cmdret_t  _c_mode(char mode);
static cmdret_t  _c_mget(ch_t *, ch_t *, int rflag, char ** files);
#ifdef MSSFTP
static cmdret_t  _c_open(ch_t *);
#else /* MSSFTP */
static cmdret_t  _c_open(ch_t *, char *, char *, char *, int);
#endif /* MSSFTP */
static cmdret_t  _c_order(char * order);
static cmdret_t  _c_parallel(int count);
static cmdret_t  _c_passive();
static cmdret_t  _c_pbsz(long long length);
static cmdret_t  _c_prot(char);
static cmdret_t  _c_pget(ch_t*, ch_t*,globus_off_t, globus_off_t, char*, char*);
static cmdret_t  _c_pwd(ch_t *);
static cmdret_t  _c_quit(ch_t *, ch_t *);
static cmdret_t  _c_quote(ch_t *, char ** words);
static cmdret_t  _c_rename(ch_t *, char * sfile, char * dfile);
static cmdret_t  _c_retry(int cnt);
static cmdret_t  _c_resume(int dflag, char * resume);
static cmdret_t  _c_rm(ch_t *, int rflag, char ** files);
static cmdret_t  _c_rmdir(ch_t *, char ** dirs);
static cmdret_t  _c_runique();
static cmdret_t  _c_shell(char ** args);
static cmdret_t  _c_size(ch_t *, char ** files);
static cmdret_t  _c_stage(ch_t *, int rflag, int t, char ** files);
static cmdret_t  _c_sunique();
static cmdret_t  _c_tcpbuf(long long size);
#ifdef MSSFTP
static cmdret_t  _c_type(char *);
#endif /* MSSFTP */
static cmdret_t  _c_versions();
static cmdret_t  _c_wait();
static cmdret_t  _c_link(ch_t *, char * oldfile, char * newfile);
static cmdret_t  _c_symlink(ch_t *, char * oldfile, char * newfile);

static errcode_t _c_get_ml(ch_t * ch, char * target, int type, ml_t ** mlp);
static cmdret_t _c_xfer(ch_t       * sch, 
                        ch_t       * dch, 
                        int          rflag, 
                        char       * sfile, 
                        char       * dfile, 
                        int          unique, 
                        globus_off_t soff, 
                        globus_off_t slen);

static cmdret_t
_c_xfer_file(ch_t * sch,
             ch_t * dch,
             char * src,
             char * dst,
             int    unique,
             globus_off_t soff, 
             globus_off_t slen);

static cmdret_t
_c_list_normal(ch_t * ch, char * path, char * ofile);

static cmdret_t
_c_list_mlsx(ch_t * ch, char * path, char * ofile, int rflag);

static cmd_t gcmdlist [] = {
	{ _c_shell,  "!", C_A_OSTRINGS,
"Run the command using a shell on the local machine. If no command is given,\n"
"invoke an interactive shell.\n",
"! [command]\n", NULL},

	{ _c_help,    "?", C_A_OSTRING,
"If [command] is given, print a (hopefully) helpful blurb about [command].\n"
"Otherwise, list all commands.\n",
"? [command]\n", NULL},

	{ _c_active,  "active", C_A_NOARGS,
"Change to ACTIVE mode which causes the server to initiate the data\n"
"connection. The default is PASSIVE mode unless the variable\n"
"UBERFTP_ACTIVE_MODE is set in the environment. If you are behind a\n"
"firewall you must use PASSIVE mode.\n",
"active\n", NULL},

	{ _c_ascii, "ascii", C_A_NOARGS,
"Change the data transfer type to ASCII which causes the server to do some\n"
"simple transformations to the file being transferred. This is mostly useful\n"
"for changing EOL (end of line) in text files when moving between platforms.\n"
"This option is almost never necessary today. The default is BINARY mode\n"
"also known as IMAGE mode.\n",
"ascii\n", NULL},

	{ _c_binary,  "binary", C_A_NOARGS,
"Change the data transfer type to BINARY (aka IMAGE) which causes the server\n"
"to not perform transformations to the file being transferred. This is the\n"
"default and is faster than an ASCII transfer.\n",
"binary\n", NULL},

	{ _c_blksize, "blksize", C_A_OLONG,
"Change the size of the memory buffer used to read and write data to disks\n"
"to <size> bytes. The default block size is 1024*1024 (1048576) bytes. The\n"
"block size can be increased to improve file transfer performance. This is\n"
"not related to the extended block mode block size used to determine the\n"
"ratio of data to header for data transferred on the data channel.\n",
"blksize [size]\n", NULL},

	{ _c_bugs, "bugs", C_A_NOARGS,
"Prints information regarding bug reporting and feature requests.\n",
"bugs\n", NULL},

	{ _c_quit,    "bye", C_A_RCH_1|C_A_LCH_2|C_A_NOARGS,
"Close all control and data connections and exit.\n",
"bye\n", NULL},

	{ _c_cat,     "cat", C_A_RCH_1|C_A_STRINGS,
"Print the contents of the remote file(s) to stdout.\n",
"cat file1 [file2 ... filen]\n", NULL},

	{ _c_chdir,   "cd", C_A_RCH_1|C_A_OSTRING,
"Change the remote working directory to [dir]. If [dir] is not given,\n"
"the client will make every attempt to change to the user's home directory.\n"
"'~' expansion and '-' previous directory are supported.\n",
"cd [dir]\n", NULL},

	{ _c_cdup,   "cdup", C_A_RCH_1|C_A_NOARGS,
"Change the remote working directory up one level.\n",
"cdup\n", NULL},

	{ _c_chgrp,   "chgrp", C_A_RCH_1|C_A_OPT_r|C_A_2STRINGPLUS,
"Change group ownership on the remote object(s).\n",
"chgrp [-r] group object [object2 ... objectn]\n",
"-r   Recursively chgrp everything in the given directory.\n"},

	{ _c_chmod,   "chmod", C_A_RCH_1|C_A_OPT_r|C_A_PERMS,
"Change permissions on the remote object(s).\n",
"chmod [-r] perms object [object2 ... objectn]\n",
"-r   Recursively chmod everything in the given directory.\n"},

	{ _c_cksum,		"cksum", C_A_OSTRING,
"Enable file cksum comparison after each file transfer. This only works with\n"
"NCSA's mass storage system.\n",
"cksum [on|off]\n",
"on    Enable checksum comparison\n"
"off   Disable checksum comparison\n"},

	{ _c_cos, "cos", C_A_OSTRING,
"Sets the class of service to [name] on the FTP service if the service\n"
"supports it. If [name] is omitted, the current class of service is printed.\n",
"cos [name]\n", NULL},

	{ _c_close,    "close",  C_A_RCH_1|C_A_NOARGS,
"Close the control connection to the remote host.\n",
"close\n", NULL},

	{ _c_dcau,	"dcau", C_A_OCH_OSTRING,
"Change the data channel authentication settings. If the service does not\n"
"support DCAU, these settings are ignored.\n",
"dcau [N|A|S <subject>]\n",
"N  Disabled dcau.\n"
"A  Expect the remote identity to be mine. (Default)\n"
"S <subject> Expect the remote identity to be <subject>.\n"},

	{ _c_debug,   "debug", C_A_OINT,
"Turn debug statements on/off. If no value is given, this command will\n"
"toggle between debug(2) and non debug(1) mode. Otherwise the debug level\n"
"is set to the given level.\n",
"debug [0-3]\n",
"0  Only errors are printed\n"
"1  Default. Errors and some helpful messages are printed\n"
"2  Print useful control channel information\n"
"3  Print all information\n"},

#ifdef MSSFTP
	{ _c_rm,     "delete", C_A_RCH_1|C_A_OPT_r|C_A_STRINGS,
"Alias for rm. This command has been deprecated.\n",
"delete [-r] object1 [object1...objectn]\n",
"-r   Recursively remove the given directory.\n"},
#endif /* MSSFTP */

	{ _c_list,    "dir", C_A_RCH_1|C_A_OPT_r|C_A_2OSTRINGS,
"List the contents of the remote target directory. If [target] is not given,\n"
"then the current working directory is used.\n",
"dir [-r] [target [file]]\n",
"-r      Recursively list [target].\n"
"target  Directory or file to list. '.' is used by default.\n"
"file    Write listing to [file].\n"},

#ifdef MSSFTP
	{ _c_close,    "disconnect",  C_A_RCH_1|C_A_NOARGS,
"Alias for close. This command has been deprecated.\n",
"disconnect\n", NULL},
#endif /* MSSFTP */

	{ _c_family, "family", C_A_OSTRING,
"Sets the tape family to [name] on the FTP service if the service\n"
"supports it. If [name] is omitted, the current family is printed.\n",
"family [name]\n", NULL},

	{ _c_get,     "get", C_A_RCH_1|C_A_LCH_2|C_A_OPT_r|C_A_STRINGPLUS,
"Retreive file(s) from the remote service. If <source> implies multiple\n"
"transfers, either through regular expressions or by using the recursive\n"
"feature, then [destination] must be a directory. If [destination] is not\n"
"specified, <source> is used.\n",
"get [-r] <source> [destination]\n",
"-r   Recursively transfer the given directory.\n"},

	{ _c_glob, "glob", C_A_OSTRING,
"Enable or disable filename globbing. If no option is given, this command\n"
"will toggle the current setting.\n",
"glob [on|off]\n",
"on    Enable filename globbing\n"
"off   Disable filename globbing\n"},

	{ _c_hash,    "hash", C_A_NOARGS,
"Print hash marks during data transfers. This does not work during third\n"
"party transfers.\n",
"hash\n", NULL},

	{ _c_help,    "help", C_A_OSTRING,
"If [command] is given, print a (hopefully) helpful blurb about [command].\n"
"Otherwise, list all commands.\n",
"help [command]\n", NULL},

	{ _c_keepalive, "keepalive", C_A_OINT,
"Attempts to keep the control channel from being blocked by firewalls during\n"
"long data channel operations. UberFTP sends a NOOP command to the service\n"
"at intervals equal to the specified number of seconds. Setting it to zero\n"
"will disable keepalive. If seconds are not given, the current timeout is\n"
"displayed. This feature is disabled by default.\n",
"keepalive [seconds]\n",
"seconds  number of seconds between NOOPs. Disabled if zero.\n"},

	{ _c_cat,   "lcat", C_A_LCH_1|C_A_STRINGS,
"Print the contents of the local file(s) to stdout.\n",
"lcat file1 [file2 ... filen]\n", NULL},

	{ _c_chdir, "lcd", C_A_LCH_1|C_A_OSTRING,
"Change the local working directory to [dir]. If [dir] is not given,\n"
"the client will make every attempt to change to the user's home directory.\n"
"'~' expansion and '-' previous directory are supported.\n",
"lcd [dir]\n", NULL},

	{ _c_cdup,   "lcdup", C_A_LCH_1|C_A_NOARGS,
"Change the local working directory up one level.\n",
"lcdup\n", NULL},

	{ _c_chgrp,   "lchgrp", C_A_LCH_1|C_A_OPT_r|C_A_2STRINGPLUS,
"Change group ownership on the local object(s).\n",
"lchgrp [-r] group object [object2 ... objectn]\n",
"-r   Recursively chgrp everything in the given directory.\n"},

	{ _c_chmod, "lchmod", C_A_LCH_1|C_A_OPT_r|C_A_PERMS,
"Change permissions on the local object(s).\n",
"lchmod [-r] perms object [object2 ... objectn]\n",
"-r   Recursively chmod everything in the given directory.\n"},

#ifndef MSSFTP
	{ _c_close,  "lclose", C_A_LCH_1|C_A_NOARGS,
"Close the control connection to the local host.\n",
"lclose\n", NULL},
#endif /* !MSSFTP */

#ifdef MSSFTP
	{ _c_rm,     "ldelete", C_A_LCH_1|C_A_OPT_r|C_A_STRINGS,
"Alias for lrm. This command has been deprecated.\n",
"ldelete [-r] object1 [object1...objectn]\n",
"-r   Recursively remove the given directory.\n"},
#endif /* MSSFTP */

	{ _c_list,    "ldir", C_A_LCH_1|C_A_OPT_r|C_A_2OSTRINGS,
"List the contents of the local target directory. If [target] is not given,\n"
"then the current working directory is used.\n",
"ldir [-r] [target [file]]\n",
"-r      Recursively list [target].\n"
"target  Directory or file to list. '.' is used by default.\n"
"file    Write listing to [file].\n"},

	{ _c_link, "link", C_A_RCH_1|C_A_2STRINGS,
"Creates a hardlink to 'oldfile' on the remote service.\n",
"link oldfile newfile\n", NULL},

	{ _c_link, "llink", C_A_LCH_1|C_A_2STRINGS,
"Creates a hardlink to 'oldfile' on the local service.\n",
"llink oldfile newfile\n", NULL},

	{ _c_list,  "lls", C_A_LCH_1|C_A_OPT_r|C_A_2OSTRINGS,
"List the contents of the local target directory. If [target] is not given,\n"
"then the current working directory is used.\n",
"lls [-r] [target [file]]\n",
"-r      Recursively list [target].\n"
"target  Directory or file to list. '.' is used by default.\n"
"file    Write listing to [file].\n"},

	{ _c_lscos,       "llscos", C_A_LCH_1|C_A_NOARGS,
"List the available COS on the local server (if supported).\n",
"llscos\n", NULL},

	{ _c_lsfam,       "llsfam", C_A_LCH_1|C_A_NOARGS,
"List the available families on the local server (if supported).\n",
"llsfam\n", NULL},

#ifdef MSSFTP
	{ _c_rm,     "lmdelete", C_A_LCH_1|C_A_OPT_r|C_A_STRINGS,
"Alias for lrm. This command has been deprecated.\n",
"lmdelete [-r] object1 [object1...objectn]\n",
"-r   Recursively remove the given directory.\n"},
#endif /* MSSFTP */

	{ _c_mkdir,  "lmkdir", C_A_LCH_1|C_A_STRINGS,
"Create the local directory(ies).\n",
"lmkdir  dir1 [dir2 ... dirn]\n", NULL},

#ifndef MSSFTP
	{ _c_open,   "lopen", C_A_LCH_1|C_A_OPT_u|C_A_OPT_p|C_A_OPT_P|C_A_STRING,
"Opens a control channel to <host> and that host becomes the 'local' machine.\n"
"After using lopen, all local (l*) commands perform their respective\n"
"operations on <host> rather than the local machine. This is how third\n"
"party transfers are accomplished. GSI authentication is used unless the\n"
"-p option is used.\n",
"lopen [-P port] [-u user] [-p pass | X] <host>\n",
"-P port   Connect to port (Default 2811 for GSI, 21 for password).\n"
"-u user   Connect as alternate user.\n"
"-p pass | X\n"
"          Use password 'pass' when authenticating with 'host'.\n"
"          If 'pass' equals 'X', read the password from STDIN with\n"
"          character echoing turned off.\n"
"host      Connect to host.\n"},
#endif /* !MSSFTP */

	{ _c_pwd,    "lpwd", C_A_LCH_1|C_A_NOARGS,
"Prints the current local working directory.\n",
"lpwd\n", NULL},

	{ _c_quote, "lquote", C_A_LCH_1|C_A_STRINGS,
"Pass <cmd> to the local FTP service. This allows the user to use\n"
"server-specific commands that are not available through the uberftp\n"
"interface.\n",
"lquote <cmd>\n", NULL},

	{ _c_rename, "lrename", C_A_LCH_1|C_A_2STRINGS,
"Rename the local object <src> to <dst>.\n",
"lrename <src> <dst>\n", NULL},

	{ _c_rm,     "lrm", C_A_LCH_1|C_A_OPT_r|C_A_STRINGS,
"Removes the local file system object(s).\n",
"lrm [-r] object1 [object1...objectn]\n",
"-r   Recursively remove the given directory.\n"},

	{ _c_rmdir, "lrmdir", C_A_LCH_1|C_A_STRINGS,
"Removes the given directories from the local service.\n",
"lrmdir dir1 [dir2...dirn]\n", NULL},

	{ _c_list,       "ls", C_A_RCH_1|C_A_OPT_r|C_A_2OSTRINGS,
"List the contents of the remote target directory. If [target] is not given,\n"
"then the current working directory is used.\n",
"ls [-r] [target [file]]\n",
"-r      Recursively list [target].\n"
"target  Directory or file to list. '.' is used by default.\n"
"file    Write listing to [file].\n"},

	{ _c_lscos,       "lscos", C_A_RCH_1|C_A_NOARGS,
"List the available COS on the remote server (if supported).\n",
"lscos\n", NULL},

	{ _c_lsfam,       "lsfam", C_A_RCH_1|C_A_NOARGS,
"List the available families on the remote server (if supported).\n",
"lsfam\n", NULL},

	{ _c_size, "lsize", C_A_LCH_1|C_A_STRINGS,
"Prints the size of the given object(s).\n",
"lsize file1 [file2...filen]\n", NULL },

	{ _c_stage, "lstage", C_A_LCH_1|C_A_OPT_r|C_A_INT_STRINGS,
"Attempt to stage all matching files within the given number of seconds\n"
"on the local service.\n",
"lstage [-r] seconds object1 [object2...objectn]\n",
"seconds  number of seconds to attempt staging\n"
"-r       Recursively stage all files in the given subdirectory.\n"},

	{ _c_symlink, "lsymlink", C_A_LCH_1|C_A_2STRINGS,
"Creates a symlink to 'oldfile' on the local service.\n",
"lsymlink oldfile newfile\n", NULL},

#ifdef MSSFTP
	{ _c_rm,     "mdelete", C_A_RCH_1|C_A_OPT_r|C_A_STRINGS,
"Alias for rm. This command has been deprecated.\n",
"mdelete [-r] object1 [object1...objectn]\n",
"-r   Recursively remove the given directory.\n"},
#endif /* MSSFTP */

	{ _c_mget,       "mget", C_A_RCH_1|C_A_LCH_2|C_A_OPT_r|C_A_STRINGS,
"Retrieve file(s) from the remote service. This is similiar to making\n"
"multiple calls to get without specifying a destination.\n",
"mget [-r] object1 [object2...objectn]\n",
"-r   Recursively transfer the given directory.\n"},

	{ _c_mkdir, "mkdir", C_A_RCH_1|C_A_STRINGS,
"Create the remote directory.\n",
"mkdir <dir>\n", NULL},

	{ _c_mode,       "mode", C_A_OCHAR,
"Toggle the data transfer mode between Streams mode and Extended Block\n"
"mode. The default is Streams mode. If no option is given, it will\n"
"display the current mode.\n",
"mode [E|S]\n",
"E   Extended block mode\n"
"S   Streams mode\n"},

	{ _c_mget,       "mput", C_A_LCH_1|C_A_RCH_2|C_A_OPT_r|C_A_STRINGS,
"Store file(s) to the remote service. This is similiar to making\n"
"multiple calls to put without specifying a destination.\n",
"mput [-r] object1 [object2...objectn]\n",
"-r   Recursively transfer the given directory.\n"},

#ifndef MSSFTP
	{ _c_open,  "open", C_A_RCH_1|C_A_OPT_u|C_A_OPT_p|C_A_OPT_P|C_A_STRING,
"Opens a control channel to <host> and that host becomes the 'remote'\n"
"machine. GSI authentication is used unless the -p option is used.\n",
"open [-P port] [-u user] [-p pass | X] <host>\n",
"-P port   Connect to port (Default 2811 for GSI, 21 for password).\n"
"-u user   Connect as alternate user.\n"
"-p pass | X\n"
"          Use password 'pass' when authenticating with 'host'.\n"
"          If 'pass' equals 'X', read the password from STDIN with\n"
"          character echoing turned off.\n"
"host      Connect to host.\n"},
#else /* !MSSFTP */
	{ _c_open,  "open", C_A_RCH_1|C_A_NOARGS,
"Opens a control channel to MSS and that host becomes the 'remote'\n"
"machine. GSI authentication is used.\n",
"open\n", NULL},
#endif /* !MSSFTP */

	{ _c_order, "order", C_A_OSTRING,
"Changes the order of lists returned from ls and lls to the given scheme.\n"
"If <type> is not given, the current order is displayed.\n",
"order <type>\n",
"type    Ordering scheme to use. Value options are:\n"
"           none  Do not order listings\n"
"           name  Order listings by name\n"
"           size  Order listings by size\n"
"           type  Order listings by type\n"},

	{ _c_parallel, "parallel",  C_A_OINT,
"Set the number of parallel data connections to <number>. This is only\n"
"useful for extended block mode transfers. The default number of data\n"
"connections is one. If no number is given, the current setting for the\n"
"number of parallel connects is printed.\n",
"parallel [number]\n", NULL},

	{ _c_passive,  "passive", C_A_NOARGS,
"Change to PASSIVE mode which causes the client to initiate the data\n"
"connection. This is the default mode unless the variable\n"
"UBERFTP_ACTIVE_MODE is set in the environment. If you are behind a\n"
"firewall you must use PASSIVE mode.\n",
"passive\n", NULL},

	{ _c_pbsz,	"pbsz", C_A_OLONG,
"Change the length of the protection buffer. The protection buffer is used\n"
"to encrypt data on the data channel. The length of the protection buffer\n"
"represents the largest encoded message that is allowed on the data channel.\n"
"By default, the protection buffer is grown to match the internal buffer\n"
"used. For efficient transfers, pbsz should be sufficiently larger than\n"
"blksize so that the wrapped buffer fits within the protection buffer.\n"
"Otherwise, the blksize buffer is broken into multiple pieces so that each\n"
"write is less than pbsz when wrapped. If [size] is not given, the current\n"
"size is displayed.\n",
"pbsz [size]\n",
"size   length of protection buffer. 0 will set it to its default.\n"},

	{ _c_pget,  "pget", C_A_RCH_1|C_A_LCH_2|C_A_2OFF,
"Retrieve only the specified portion of the file(s). If srcfile is a regular\n"
"expression and expands to multiple files, and destination is given,\n"
"destination must refer to a directory.\n",
"pget offset size srcfile [destfile]\n",
"offset   Offset within the file\n"
"size     Amount of data to retrieve\n"
"srcfile  Name of remote file\n"
"destfile Name of local file. srcfile is used if destfile\n"
"         is not specified\n"},

	{ _c_pget,  "pput",  C_A_LCH_1|C_A_RCH_2|C_A_2OFF,
"Store only the specified portion of the file(s). If srcfile is a regular\n"
"expression and expands to multiple files, and destination is given,\n"
"destination must refer to a directory.\n",
"pput offset size srcfile [destfile]\n",
"offset   Offset within the file\n"
"size     Amount of data to retrieve\n"
"srcfile  Name of remote file\n"
"destfile Name of local file. srcfile is used if destfile\n"
"         is not specified\n"},

	{ _c_prot,	"prot", C_A_OCHAR,
"This command configures the level of security on the data channel after\n"
"data channel authentication has completed. Clear means that the data will\n"
"not be protected. Safe means that the data will be integrity protected\n"
"meaning that altered data will be detected. Confidential means that the data\n"
"will be unreadable to third parties. Private mode means the data will be\n"
"confidential and safe.\n",
"prot [C|S|E|P]\n",
"C  Set protection level to clear.\n"
"S  Set protection level to safe.\n"
"E  Set protection level to confidential.\n"
"P  Set protection level to private.\n"},

	{ _c_get,   "put", C_A_LCH_1|C_A_RCH_2|C_A_OPT_r|C_A_STRINGPLUS,
"Store file(s) to the remote service. If <source> implies multiple\n"
"transfers, either through regular expressions or by using the recursive\n"
"feature, then [destination] must be a directory. If [destination] is not\n"
"specified, <source> is used.\n",
"put [-r] <source> [destination]\n",
"-r   Recursively transfer the given directory.\n"},

	{ _c_pwd,  "pwd", C_A_RCH_1|C_A_NOARGS,
"Prints the current working directory.\n",
"pwd\n", NULL},

	{ _c_quit,   "quit", C_A_RCH_1|C_A_LCH_2|C_A_NOARGS,
"Close all control and data connections and exit.\n",
"quit\n", NULL},

	{ _c_quote,  "quote", C_A_RCH_1|C_A_STRINGS,
"Pass <cmd> to the remote FTP service. This allows the user to use\n"
"server-specific commands that are not available through the uberftp\n"
"interface.\n",
"quote <cmd>\n", NULL},

#ifdef MSSFTP
	{ _c_get,     "recv", C_A_RCH_1|C_A_LCH_2|C_A_OPT_r|C_A_STRINGPLUS,
"Alias for get. This command has been deprecated.\n",
"recv [-r] <source> [destination]\n",
"-r   Recursively transfer the given directory.\n"},
#endif /* MSSFTP */

	{ _c_rename, "rename", C_A_RCH_1|C_A_2STRINGS,
"Rename the remote object <src> to <dst>.\n",
"rename <src> <dst>\n", NULL},

	{ _c_resume,	"resume", C_A_OPT_d|C_A_OSTRING,
"Sets a restart point for recursive transfers. If a long recursive transfer\n"
"fails, you can set resume to the path that failed and UberFTP will skip\n"
"all file and directory creations up to the given path.\n",
"resume [-d] [path]\n",
"path   Path to resume transfer at. If path is not given, print the current\n"
"       resume target.\n"
"-d     Remove the current resume path.\n"},

	{ _c_retry,     "retry", C_A_OINT,
"Configures retry on failed commands that have transient errors. cnt\n"
"represents the number of times a failed command is retried. A value of\n"
"zero effectively disables retry. Zero is the default. If no value is given\n"
"the current setting is displayed.\n",
"retry [cnt]\n",
"cnt    Number of times a failed command is retried.\n"},

	{ _c_rm,     "rm", C_A_RCH_1|C_A_OPT_r|C_A_STRINGS,
"Removes the remote file system object(s).\n",
"rm [-r] object1 [object1...objectn]\n",
"-r   Recursively remove the given directory.\n"},

	{ _c_rmdir,  "rmdir", C_A_RCH_1|C_A_STRINGS,
"Removes the given directories from the remote service.\n",
"rmdir dir1 [dir2...dirn]\n", NULL},

	{ _c_runique,  "runique", C_A_NOARGS,
"Toggles the client to store files using unique names during get operations.\n",
"runique\n", NULL},

#ifdef MSSFTP
	{ _c_get,     "send", C_A_LCH_1|C_A_RCH_2|C_A_OPT_r|C_A_STRINGPLUS,
"Alias for put. This command has been deprecated.\n",
"send [-r] <source> [destination]\n",
"-r   Recursively transfer the given directory.\n"},
#endif /* MSSFTP */

	{ _c_size, "size", C_A_RCH_1|C_A_STRINGS,
"Prints the size of the given object(s).\n",
"size file1 [file2...filen]\n", NULL },

	{ _c_stage, "stage", C_A_RCH_1|C_A_OPT_r|C_A_INT_STRINGS,
"Attempt to stage all matching files within the given number of seconds\n"
"on the remote service.\n",
"stage [-r] seconds object1 [object2...objectn]\n",
"seconds  number of seconds to attempt staging\n"
"-r       Recursively stage all files in the given subdirectory.\n"},

	{ _c_sunique,  "sunique", C_A_NOARGS,
"Toggles the client to store files using unique names during put operations.\n",
"sunique\n", NULL},

	{ _c_symlink, "symlink", C_A_RCH_1|C_A_2STRINGS,
"Creates a symlink to 'oldfile' on the remote service.\n",
"symlink oldfile newfile\n", NULL},

	{ _c_tcpbuf, "tcpbuf", C_A_OLONG,
"Set the data channel TCP buffer size to [size] bytes. If [size] is not\n"
"given, the current TCP buffer size will be printed.\n",
"tcpbuf [size]\n", NULL},

#ifdef MSSFTP
	{ _c_type, "type", C_A_OSTRING,
"Sets the transfer type. If no type is given, it reports the current type.\n",
"type [ascii|binary]\n", NULL},

	{ _c_debug,   "verbose", C_A_OINT,
"Alias for debug. This command has been deprecated.\n",
"verbose [0-3]\n",
"0  Only errors are printed\n"
"1  Default. Errors and some helpful messages are printed\n"
"2  Print useful control channel information\n"
"3  Print all information\n"},
#endif /* MSSFTP */

	{ _c_versions, "versions", C_A_NOARGS,
"Prints the versions of all Globus modules being used.\n",
"versions\n", NULL},

	{ _c_wait, "wait", C_A_NOARGS,
"Toggles whether the client should wait for files to stage before attempting\n"
"to retrieve them.\n",
"wait\n", NULL},

	{ NULL, NULL}
};

void
cmd_init()
{
	glch.lh = l_init(UnixInterface); /* Local connection handle. */
	grch.lh = l_init(NcInterface); /* Remote connection handle. */
}


cmdret_t
cmd_intrptr(char * cmd)
{
	cmdret_t    cr     = CMD_SUCCESS;
	char      * token  = NULL;
	char      * nxttok = NULL;
	char      * sc     = NULL;
	char      * buf    = NULL;

	buf = cmd = Strdup(cmd);

	for (; cmd && *cmd && cr == CMD_SUCCESS; cmd = (sc ? sc+1 : NULL))
	{
		sc = StrchrEsc(cmd, ";,");
		if (sc)
			*sc = '\0';

		nxttok = cmd;
		do 
		{
			token = StrtokEsc(nxttok, ' ', &nxttok);
			cmd   = NULL;
			cr    = _c_lex(token);

		} while (token && cr == CMD_SUCCESS);
	}

	FREE(buf);
	return cr;
}

static cmdret_t
_c_lex(char * token)
{
	static int          state = C_A_CMD;
	static int          dflag = 0;
	static int          rflag = 0;
	static int          rparg = 0;
	static int          rParg = 0;
	static int          ruarg = 0;
	static cmd_t     *  cmd   = NULL;
	static char      *  parg  = NULL;
	static char      *  uarg  = NULL;
	static char      ** strs = NULL;
	static char      *  str  = NULL;
	static char         chr;
	static int          perms = 0;
	static int          Parg  = -1;
	static int          scnt  = 0;
	static int          ival  = -1;
	static globus_off_t lval1 = -1;
	static globus_off_t lval2 = -1;

	cmdret_t cr    = CMD_SUCCESS;

	if ((ruarg || rParg || rparg) && !token)
	{
		fprintf(stderr, "Illegal syntax.\n");
		goto error;
	}

	if (ruarg)
	{
		ruarg = 0;
		uarg = Strdup(token);
		return CMD_SUCCESS;
	}

	if (rparg)
	{
		rparg = 0;
		parg = Strdup(token);
		return CMD_SUCCESS;
	}

	if (rParg)
	{
		if (!IsInt(token))
		{
			fprintf(stderr, "%s must be an integer.\n", token);
			goto error;
		}

		rParg = 0;
		Parg = strtol(token, NULL, 0);
		return CMD_SUCCESS;
	}

	if (!token)
	{
		switch (state & ~C_A_OPT_MASK)
		{
		case C_A_CMD:
			return CMD_SUCCESS;

		case C_A_OCH_OSTRING:
		case C_A_OCHAR:
		case C_A_NOARGS:
		case C_A_OINT:
		case C_A_OLONG:
		case C_A_OSTRINGS:
		case C_A_OSTRING:
		case C_A_2OSTRINGS:
			switch(cmd->ctype)
			{
			case C_A_NOARGS:
				cr = ((cmdret_t (*)())cmd->func)();
				break;
			case C_A_OINT:
				cr = ((cmdret_t (*)(int))cmd->func)(ival);
				break;
			case C_A_OLONG:
				cr = ((cmdret_t (*)(long long))cmd->func)(lval1);
				break;
			case C_A_OPT_d|C_A_OSTRING:
				cr = ((cmdret_t (*)(int, char *))cmd->func)(dflag, 
				                                            strs?strs[0]:NULL);
				break;
			case C_A_STRING:
				cr = ((cmdret_t (*)(char*))cmd->func)(strs[0]);
				break;
			case C_A_OSTRING:
				cr = ((cmdret_t (*)(char*))cmd->func)(strs?strs[0]:NULL);
				break;
			case C_A_OCHAR:
				cr = ((cmdret_t (*)(char))cmd->func)(chr);
				break;
			case C_A_OCH_OSTRING:
				cr = ((cmdret_t (*)(char, char *))cmd->func)
				                              (chr, strs?strs[0]:NULL);
				break;
			case C_A_OSTRINGS:
				cr = ((cmdret_t (*)(char**))cmd->func)(strs);
				break;


			case C_A_RCH_1|C_A_NOARGS:
				cr = ((cmdret_t (*)(ch_t*))cmd->func)(&grch);
				break;
			case C_A_RCH_1|C_A_STRING:
				cr = ((cmdret_t (*)(ch_t*,char*))cmd->func)(&grch,strs[0]);
				break;
			case C_A_RCH_1|C_A_OSTRING:
				cr = ((cmdret_t (*)(ch_t*,char*))cmd->func)
				                                 (&grch,strs?strs[0]:NULL);
				break;
			case C_A_RCH_1|C_A_STRINGS:
				cr = ((cmdret_t (*)(ch_t*,char**))cmd->func)(&grch,strs);
				break;
			case C_A_RCH_1|C_A_2STRINGS:
				cr = ((cmdret_t (*)(ch_t*,char*,char*))cmd->func)
				                                        (&grch,strs[0],strs[1]);
				break;
			case C_A_RCH_1|C_A_OPT_r|C_A_STRINGS:
				cr = ((cmdret_t (*)(ch_t*,int,char**))cmd->func)
				                                      (&grch,rflag,strs);
				break;
			case C_A_RCH_1|C_A_OPT_r|C_A_2STRINGPLUS:
				cr = ((cmdret_t (*)(ch_t*,int,char*,char**))cmd->func)
				                                   (&grch,rflag,str,strs);
				break;
			case C_A_RCH_1|C_A_OPT_r|C_A_PERMS:
				cr = ((cmdret_t (*)(ch_t*,int,int,char**))cmd->func)
				                                   (&grch,rflag,perms,strs);
				break;
			case C_A_RCH_1|C_A_OPT_r|C_A_INT_STRINGS:
				cr = ((cmdret_t (*)(ch_t*,int,int,char**))cmd->func)
				                                   (&grch,rflag,ival,strs);
				break;
			case C_A_RCH_1|C_A_OPT_r|C_A_OSTRING:
				cr = ((cmdret_t (*)(ch_t*,int,char*))cmd->func)
				                                (&grch,rflag,strs?strs[0]:NULL);
				break;
			case C_A_RCH_1|C_A_OPT_r|C_A_2OSTRINGS:
				cr = ((cmdret_t (*)(ch_t*,int,char*,char*))cmd->func)
				                    (&grch,rflag,strs?strs[0]:NULL,strs?strs[1]:NULL);
				break;
			case C_A_RCH_1|C_A_OPT_u|C_A_OPT_p|C_A_OPT_P|C_A_STRING:
				cr = ((cmdret_t (*)(ch_t*,char*,char*,char*,int))cmd->func)
				                                (&grch,uarg,parg,strs[0],Parg);
				break;



			case C_A_LCH_1|C_A_NOARGS:
				cr = ((cmdret_t (*)(ch_t*))cmd->func)(&glch);
				break;
			case C_A_LCH_1|C_A_OSTRING:
				cr = ((cmdret_t (*)(ch_t*,char*))cmd->func)
				                             (&glch,strs?strs[0]:NULL);
				break;
			case C_A_LCH_1|C_A_STRINGS:
				cr = ((cmdret_t (*)(ch_t*,char**))cmd->func)(&glch,strs);
				break;
			case C_A_LCH_1|C_A_2STRINGS:
				cr = ((cmdret_t (*)(ch_t*,char*,char*))cmd->func)
				                                        (&glch,strs[0],strs[1]);
				break;
			case C_A_LCH_1|C_A_OPT_r|C_A_STRINGS:
				cr = ((cmdret_t (*)(ch_t*,int,char**))cmd->func)
				                                            (&glch,rflag,strs);
				break;
			case C_A_LCH_1|C_A_OPT_r|C_A_2STRINGPLUS:
				cr = ((cmdret_t (*)(ch_t*,int,char*,char**))cmd->func)
				                                   (&glch,rflag,str,strs);
				break;
			case C_A_LCH_1|C_A_OPT_r|C_A_PERMS:
				cr = ((cmdret_t (*)(ch_t*,int,int,char**))cmd->func)
				                                       (&glch,rflag,perms,strs);
				break;
			case C_A_LCH_1|C_A_OPT_r|C_A_INT_STRINGS:
				cr = ((cmdret_t (*)(ch_t*,int,int,char**))cmd->func)
				                                   (&glch,rflag,ival,strs);
				break;
			case C_A_LCH_1|C_A_OPT_r|C_A_OSTRING:
				cr = ((cmdret_t (*)(ch_t*,int,char*))cmd->func)
				                                (&glch,rflag,strs?strs[0]:NULL);
				break;
			case C_A_LCH_1|C_A_OPT_r|C_A_2OSTRINGS:
				cr = ((cmdret_t (*)(ch_t*,int,char*,char*))cmd->func)
				              (&glch,rflag,strs?strs[0]:NULL,strs?strs[1]:NULL);
				break;
			case C_A_LCH_1|C_A_OPT_u|C_A_OPT_p|C_A_OPT_P|C_A_STRING:
				cr = ((cmdret_t (*)(ch_t*,char*,char*,char*,int))cmd->func)
				                             (&glch,uarg,parg,strs[0],Parg);
				break;



			case C_A_RCH_1|C_A_LCH_2|C_A_NOARGS:
				cr = ((cmdret_t (*)(ch_t*,ch_t*))cmd->func)(&grch,&glch);
				break;
			case C_A_RCH_1|C_A_LCH_2|C_A_OPT_r|C_A_STRINGPLUS:
				cr = ((cmdret_t (*)(ch_t*,ch_t*,int,char*,char*))cmd->func)
				                            (&grch,&glch,rflag,strs[0],strs[1]);
				break;
			case C_A_RCH_1|C_A_LCH_2|C_A_OPT_r|C_A_STRINGS:
				cr = ((cmdret_t (*)(ch_t*,ch_t*,int,char**))cmd->func)
				                                      (&grch,&glch,rflag,strs);
				break;
			case C_A_RCH_1|C_A_LCH_2|C_A_2OFF:
				cr = ((cmdret_t (*)
				  (ch_t*,ch_t*,globus_off_t,globus_off_t,char*,char*))
				         cmd->func) (&grch,&glch,lval1,lval2,strs[0],strs[1]);
				break;



			case C_A_LCH_1|C_A_RCH_2|C_A_OPT_r|C_A_STRINGS:
				cr = ((cmdret_t (*)(ch_t*,ch_t*,int,char**))cmd->func)
				                                      (&glch,&grch,rflag,strs);
				break;
			case C_A_LCH_1|C_A_RCH_2|C_A_OPT_r|C_A_STRINGPLUS:
				cr = ((cmdret_t (*)(ch_t*,ch_t*,int,char*,char*))cmd->func)
				                            (&glch,&grch,rflag,strs[0],strs[1]);
				break;
			case C_A_LCH_1|C_A_RCH_2|C_A_2OFF:
				cr = ((cmdret_t (*)
				  (ch_t*,ch_t*,globus_off_t,globus_off_t,char*,char*))
				         cmd->func) (&glch,&grch,lval1,lval2,strs[0],strs[1]);
				break;

			default:
				fprintf(stderr, "Unknown command type!\n");
				goto error;
			}
			goto cleanup;
		default:
			fprintf(stderr, "Illegal syntax.\n");
			goto error;
		}
	}

	if (*token == '-' && strcmp(token, "-"))
	{
		while (*(++token) != '\0')
		{
			switch (*token)
			{
			case 'd':
				if (! (state & C_A_OPT_d))
				{
					fprintf(stderr, "Invalid option d\n");
					goto error;
				}

				if (dflag)
				{
					fprintf(stderr, "Illegal duplicate option d\n");
					goto error;
				}

				dflag++;
				break;


			case 'P':
				if (rParg || Parg != -1)
				{
					fprintf(stderr, "Illegal duplicate option P\n");
					goto error;
				}
				if (! (state & C_A_OPT_P))
				{
					fprintf(stderr, "Invalid option P\n");
					goto error;
				}
				rParg = 1;
				break;

			case 'p':
				if (rparg || parg)
				{
					fprintf(stderr, "Illegal duplicate option p\n");
					goto error;
				}
				if (! (state & C_A_OPT_p))
				{
					fprintf(stderr, "Invalid option p\n");
					goto error;
				}
				rparg = 1;
				break;

			case 'u':
				if (ruarg || uarg)
				{
					fprintf(stderr, "Illegal duplicate option u\n");
					goto error;
				}
				if (! (state & C_A_OPT_u))
				{
					fprintf(stderr, "Invalid option u\n");
					goto error;
				}
				ruarg = 1;
				break;

			case 'r':
				if (! (state & C_A_OPT_r))
				{
					fprintf(stderr, "Invalid option r\n");
					goto error;
				}
				if (((state & C_A_LCH_1) && !l_supports_glob(glch.lh)) ||
				    ((state & C_A_RCH_1) && !l_supports_glob(grch.lh)))
				{
					o_fprintf(stderr,
					          DEBUG_ERRS_ONLY,
					          "This service does not support recursion.\n");
					cmd = NULL;
					goto error;
				}

				if (rflag)
				{
					fprintf(stderr, "Illegal duplicate option r\n");
					goto error;
				}

				rflag++;
				break;

			default:
				token--;
				goto notanoption;
/*
				fprintf(stderr, "Invalid option %c\n", *token);
				goto error;
*/
			}
		}
		return CMD_SUCCESS;
	}

notanoption:
	state &= ~C_A_OPT_MASK;

	switch(state)
	{
	case C_A_CMD:
		cmd = _c_lookup(token);
		if (!cmd)
		{
			fprintf(stderr, "Unknown command %s\n", token);
			return CMD_ERR_BAD_CMD;
		}
		state = cmd->ctype;
		return CMD_SUCCESS;

	case C_A_NOARGS:
		fprintf(stderr, "Illegal syntax.\n");
		goto error;

	case C_A_INT_STRINGS:
	case C_A_OINT:
		if (!IsInt(token))
		{
			fprintf(stderr, "%s must be a positive integer.\n", token);
			goto error;
		}
		ival = strtol(token, NULL, 0);
		break;

	case C_A_OFF:
		if (!IsLongWithTag(token))
		{
			fprintf(stderr, "%s must be a positive integer.\n", token);
			goto error;
		}
		lval2 = ConvLongWithTag(token);
		break;

	case C_A_OLONG:
	case C_A_2OFF:
		if (!IsLongWithTag(token))
		{
			fprintf(stderr, "%s must be a positive integer.\n", token);
			goto error;
		}
		lval1 = ConvLongWithTag(token);
		break;

	case C_A_OSTRING:
	case C_A_STRINGPLUS:
	case C_A_2STRINGS:
	case C_A_STRING:
	case C_A_STRINGS:
	case C_A_OSTRINGS:
	case C_A_2OSTRINGS:
		strs = (char **) realloc(strs, sizeof(char *) * (scnt + 2));
		strs[scnt]   = Strdup(token);
		strs[scnt+1] = NULL;
		scnt++;
		break;

	case C_A_2STRINGPLUS:
		str = Strdup(token);
		break;

	case C_A_OCH_OSTRING:
	case C_A_OCHAR:
		chr = *token;
		break;

	case C_A_PERMS:
		if (!Validperms(token))
		{
			fprintf(stderr, "Illegal permissions: %s\n", token);
			goto error;
		}
		perms = strtol(token, NULL, 8);
		break;
	default:
		fprintf(stderr, "Illegal state (%d)!!\n", state);
		goto error;
	}

	switch (state)
	{
	case C_A_OINT:     /* 0x02 */
	case C_A_STRING:   /* 0x05 */
	case C_A_OSTRING:  /* 0x08 */
	case C_A_OCHAR:    /* 0x09 */
	case C_A_OLONG:    /* 0x11 */
		state = C_A_NOARGS;
		break;
	case C_A_STRINGS:  /* 0x03 */
	case C_A_OSTRINGS: /* 0x04 */
		state = C_A_OSTRINGS;
		break;
	case C_A_2STRINGS: /* 0x06 */
		state = C_A_STRING;
		break;
	case C_A_OCH_OSTRING:
	case C_A_STRINGPLUS:  /* 0x07 */
	case C_A_2OSTRINGS:
		state = C_A_OSTRING;
		break;
	case C_A_2OFF:     /* 0x0A */
		state = C_A_OFF;
		break;
	case C_A_INT_STRINGS: /* 0x0D */
		state = C_A_STRINGS;
		break;
	case C_A_OFF:         /* 0x0B */
		state = C_A_STRINGPLUS;
		break;
	case C_A_PERMS:
	case C_A_2STRINGPLUS:
		state = C_A_STRINGS;
		break;
	default:
		fprintf(stderr, "Illegal state (%d)!!\n", state);
		goto error;
	}

	return CMD_SUCCESS;

error:
	cr = CMD_ERR_BAD_CMD;

	/* FREE args */
	if (cmd)
		fprintf(stderr, "Usage: %s", cmd->syntax);

cleanup:
	for (; scnt > 0; scnt--)
		FREE(strs[scnt-1]);
	FREE(strs);
	FREE(str);
	FREE(parg);
	FREE(uarg);

	lval1 = -1;
	lval2 = -1;
	ival  = -1;
	chr   = '\0';
	cmd   = NULL;
	uarg  = NULL;
	parg  = NULL;
	Parg  = -1;
	perms = 0;
	state = C_A_CMD;
	rflag = 0;
	dflag = 0;
	scnt  = 0;
	rparg = 0;
	rParg = 0;
	ruarg = 0;
	strs  = NULL;
	str   = NULL;
	perms = 0;

	return cr;
}

static cmd_t * 
_c_lookup(char * cmd)
{
	int index = 0;

	for (; gcmdlist[index].name != NULL; index++)
	{
		if (strncasecmp(gcmdlist[index].name, cmd, strlen(cmd)) == 0)
			return &gcmdlist[index];
    }
	return NULL;
}


static cmdret_t
_c_active()
{
	s_setactive();
	o_printf(DEBUG_NORMAL, "Active mode\n");
	return CMD_SUCCESS;
}

static cmdret_t
_c_ascii()
{
	s_setascii();
	o_printf(DEBUG_NORMAL, "Transfer type set to ASCII\n");
	return CMD_SUCCESS;
}

static cmdret_t
_c_binary()
{
	s_setbinary();
	o_printf(DEBUG_NORMAL, "Transfer type set to IMAGE\n");
	return CMD_SUCCESS;
}

static cmdret_t
_c_blksize(long long size)
{
	if (size != -1)
		s_setblocksize(size);
	o_printf(DEBUG_NORMAL, "Block size set to %lld\n", s_blocksize());
	return CMD_SUCCESS;
}

static cmdret_t
_c_bugs()
{
	o_printf(DEBUG_ERRS_ONLY, 
	    "For feature requests or bug reporting, email gridftp@ncsa.uiuc.edu\n");
	return CMD_SUCCESS;
}

static cmdret_t
_c_cat(ch_t * ch, char ** files)
{
	int       nl     = 0;
	int       eof    = 0;
	int       retry  = 0;
	int       staged = 0;
	ml_t    * mlp    = NULL;
	fth_t   * fth    = NULL;
	errcode_t ec     = EC_SUCCESS;
	errcode_t ecl    = EC_SUCCESS;
	cmdret_t  cr     = CMD_SUCCESS;
	size_t    len    = 0;
	char    * buf    = NULL;
	globus_off_t  off = 0;

	for (; cr == CMD_SUCCESS && *files; files++, nl=0)
	{
		fth = ft_init(ch->lh, *files, 0);

		while (1)
		{
			ec = ft_get_next_ft(fth, &mlp, FTH_O_ERR_NO_MATCH);
			if (!ec && !mlp)
				break;

			if (ec != EC_SUCCESS)
				goto finish;

			if (!C_ISREG(mlp->type))
			{
				o_fprintf(stderr,
				          DEBUG_ERRS_ONLY,
				          "%s: Not a regular file.\n",
				          mlp->name);
				cr = CMD_ERR_BAD_CMD;
				goto finish;
			}

			if (s_wait())
			{
				staged = 0;

				do
				{
					ec = l_stage(ch->lh, mlp->name, &staged);
					if (!ec && !staged)
						sleep(15);
				} while (!staged && !ec);
				if (ec)
					goto finish;
			}

			retry = s_retry();
retry:
			ec = l_retrvfile(ch->lh,
			                 NULL,
			                 mlp->name,
			                 -1,
			                 -1);

			if (ec != EC_SUCCESS)
				goto finish;

			nl  = 0;
			eof = 0;
			while (ec == EC_SUCCESS && !eof)
			{
				ec = l_read(ch->lh, &buf, &off, &len, &eof);
				if (ec == EC_SUCCESS && buf != NULL)
				{
					o_fwrite(stdout, DEBUG_ERRS_ONLY, buf, len);
					nl = 1;
				}
				FREE(buf);
			}

			if (nl)
				o_printf(DEBUG_ERRS_ONLY, "\n");

finish:
			ecl = l_close(ch->lh);

			if ((ec_retry(ecl) || ec_retry(ec)) && (retry-- > 0 || !s_retry()))
			{
				ec_destroy(ecl);
				ec_destroy(ec);
				goto retry;
			}

			if (ec != EC_SUCCESS || ecl != EC_SUCCESS)
			{
				if (mlp)
					o_fprintf(stderr,
					          DEBUG_ERRS_ONLY, 
					          "Failure on %s\n", 
					          mlp->name);
				cr = CMD_ERR_GET;
				ec_print(ec);
				ec_print(ecl);
			}

			ec_destroy(ec);
			ec_destroy(ecl);
			ml_delete(mlp);
		}

		ft_destroy(fth);
	}

	return cr;
}

static cmdret_t
_c_cdup(ch_t * ch)
{
	return _c_chdir(ch, "..");
}

static cmdret_t
_c_chdir(ch_t * ch, char * path)
{
	errcode_t ec   = EC_SUCCESS;
	cmdret_t  cr   = CMD_SUCCESS;
	fth_t   * fth  = NULL;
	ml_t    * mlp  = NULL;
	ml_t    * tmlp = NULL;

	if (!path)
		path = "~";

	if (strcmp(path, "-") == 0)
	{
		C_RETRY(ec, l_chdir(ch->lh, path));
		goto cleanup;
	}

	fth = ft_init(ch->lh, path, 0);
	/* Grab the target. */
	ec  = ft_get_next_ft(fth, &mlp, FTH_O_ERR_NO_MATCH);
	if (ec != EC_SUCCESS)
		goto cleanup;

	/* Make sure there was only one. */
	ec  = ft_get_next_ft(fth, &tmlp, FTH_O_PEAK);
	if (ec != EC_SUCCESS)
		goto cleanup;

	if (tmlp)
	{
		o_fprintf(stderr,
		          DEBUG_ERRS_ONLY,
		          "Too many matches for %s.\n",
		          path);
		ml_delete(tmlp);
		cr = CMD_ERR_BAD_CMD;
		goto cleanup;
	}

	if (!C_ISDIR(mlp->type))
	{
		o_fprintf(stderr,
		          DEBUG_ERRS_ONLY,
		          "%s: Not a directory.\n",
		          mlp->name);
		cr = CMD_ERR_BAD_CMD;
		goto cleanup;
	}

	C_RETRY(ec, l_chdir(ch->lh, mlp->name));

cleanup:
	if (ec != EC_SUCCESS)
		cr = CMD_ERR_DIR_OP;

	ec_print(ec);
	ec_destroy(ec);
	ml_delete(mlp);
	ft_destroy(fth);

	return cr;
}

static cmdret_t
_c_chgrp(ch_t * ch, int rflag, char * group, char ** files)
{
	errcode_t ec = EC_SUCCESS;
	cmdret_t  cr = CMD_SUCCESS;
	fth_t   * fth  = NULL;
	ml_t    * mlp  = NULL;
	int       opts = 0;

	if (rflag)
		opts |= FTH_O_RECURSE;

	for (; cr == CMD_SUCCESS && *files; files++)
	{
		fth = ft_init(ch->lh, *files, opts);
		while (cr == CMD_SUCCESS)
		{
			ec = ft_get_next_ft(fth, &mlp, FTH_O_ERR_NO_MATCH);
			if (!ec && !mlp)
				break;

			if (!ec)
				C_RETRY(ec, l_chgrp(ch->lh, group, mlp->name));

			if (ec && mlp)
				o_fprintf(stderr, 
				          DEBUG_ERRS_ONLY,
				          "Failed to chgrp %s\n", 
				          mlp->name);
			if (ec)
				cr = CMD_ERR_OTHER;
			ec_print(ec);
			ec_destroy(ec);
			ml_delete(mlp);
		}
		ft_destroy(fth);
	}

	return cr;
}

static cmdret_t
_c_chmod(ch_t * ch, int rflag, int perms, char ** files)
{
	errcode_t ec = EC_SUCCESS;
	cmdret_t  cr = CMD_SUCCESS;
	fth_t   * fth  = NULL;
	ml_t    * mlp  = NULL;
	int       opts = 0;

	if (rflag)
		opts |= FTH_O_RECURSE;

	if (!(S_IRUSR & perms) || !(S_IXUSR & perms))
		opts |= FTH_O_REVERSE;

	for (; cr == CMD_SUCCESS && *files; files++)
	{
		fth = ft_init(ch->lh, *files, opts);
		while (cr == CMD_SUCCESS)
		{
			ec = ft_get_next_ft(fth, &mlp, FTH_O_ERR_NO_MATCH);
			if (!ec && !mlp)
				break;

			if (!ec)
				C_RETRY(ec, l_chmod(ch->lh, perms, mlp->name));

			if (ec && mlp)
			{
				o_fprintf(stderr, 
				          DEBUG_ERRS_ONLY,
				          "Failed to chmod %s\n", 
				          mlp->name);
				cr = CMD_ERR_OTHER;
			}
			ec_print(ec);
			ec_destroy(ec);
			ml_delete(mlp);
		}
		ft_destroy(fth);
	}

	return cr;
}

static cmdret_t
_c_close(ch_t * ch)
{
	errcode_t   ec  = EC_SUCCESS;
	char      * msg = NULL;

	ec = l_disconnect(ch->lh, &msg);

	if (msg)
		o_printf(DEBUG_NORMAL, msg);

	ec_print(ec);
	ec_destroy(ec);

	return CMD_SUCCESS;
}

static cmdret_t
_c_cksum(char * val)
{
	if (val)
	{
		if (strcmp(val, "on") == 0)
			s_setcksum(1);
		else if (strcmp(val, "off") == 0)
			s_setcksum(0);
		else
		{
			o_fprintf(stderr, DEBUG_ERRS_ONLY, "Illegal value %s\n", val);
			return CMD_ERR_BAD_CMD;
		}
	}

	o_printf(DEBUG_NORMAL, "cksum is %s.\n", s_cksum() ? "enabled":"disabled");
	return CMD_SUCCESS;
}

static cmdret_t
_c_cos(char * cos)
{
	if (cos)
		s_setcos(cos);

	if (s_cos())
		o_printf(DEBUG_NORMAL, "Class of service set to %s\n", s_cos());
	else
		o_printf(DEBUG_NORMAL, "Class of service set to the default.\n");
	return CMD_SUCCESS;
}

static cmdret_t
_c_dcau(char mode, char * subject)
{
	switch(mode)
	{
	case 'N':
	case 'n':
		if (subject)
		{
			o_fprintf(stderr, DEBUG_ERRS_ONLY, "No subject with this mode.\n");
			return CMD_ERR_BAD_CMD;
		}
		s_setdcau(0, NULL);
		break;

	case 'A':
	case 'a':
		if (subject)
		{
			o_fprintf(stderr, DEBUG_ERRS_ONLY, "No subject with this mode.\n");
			return CMD_ERR_BAD_CMD;
		}
		s_setdcau(1, NULL);
		break;

	case 'S':
	case 's':
		if (!subject)
		{
			o_fprintf(stderr,
			          DEBUG_ERRS_ONLY, 
			          "Subject expected with this mode.\n");
			return CMD_ERR_BAD_CMD;
		}
		s_setdcau(2, subject);
		break;

	case '\0':
		break;
	default:
		o_fprintf(stderr, DEBUG_ERRS_ONLY, "Illegal mode %c\n", mode);
		return CMD_ERR_BAD_CMD;
	}

	switch (s_dcau())
	{
	case 0:
		o_printf(DEBUG_NORMAL, "DCAU disabled\n");
		break;
	case 1:
		o_printf(DEBUG_NORMAL, "DCAU using authentication with self\n");
		break;
	case 2:
		o_printf(DEBUG_NORMAL, "DCAU using authentication with subject.\n");
		o_printf(DEBUG_NORMAL, "subject: %s\n", s_dcau_subject());
		break;
	}
	return CMD_SUCCESS;
}

static cmdret_t
_c_debug(int lvl)
{
	if (lvl != -1)
		s_setdebug(lvl);
	else
		s_setdebug(s_debug() > 1 ? 1 : 2);

	o_printf(DEBUG_ERRS_ONLY,
	         "Debug level set at %d (%s)\n", 
	         s_debug(), s_debug()>1 ? "ON":"OFF");

	return CMD_SUCCESS;
}

static cmdret_t
_c_family(char * family)
{
	if (family)
		s_setfamily(family);

	if (s_family())
		o_printf(DEBUG_NORMAL, "Family set to %s\n", s_family());
	else
		o_printf(DEBUG_NORMAL, "Family set to the default.\n");
	return CMD_SUCCESS;
}

static cmdret_t
_c_glob(char * val)
{
	if (val)
	{
		if (strcmp(val, "on") == 0)
			s_setglob(1);
		else if (strcmp(val, "off") == 0)
			s_setglob(0);
		else
		{
			o_fprintf(stderr, DEBUG_ERRS_ONLY, "Illegal value %s\n", val);
			return CMD_ERR_BAD_CMD;
		}
	} else
	{
		s_setglob(! s_glob());
	}

	o_printf(DEBUG_NORMAL, "globbing is %s.\n", s_glob() ? "enabled":"disabled");
	return CMD_SUCCESS;
}

static cmdret_t
_c_get(ch_t * sch, ch_t * dch, int rflag, char * sfile, char * dfile)
{
	int unique = 0;

	if (dch == &grch)
		unique = s_sunique();
	if (dch == &glch)
		unique = s_runique();

	return _c_xfer(sch, 
	               dch, 
	               rflag, 
	               sfile, 
	               dfile, 
	               unique, 
	               -1, 
	               -1);
}

static cmdret_t
_c_hash()
{
	s_sethash();
	o_printf(DEBUG_NORMAL, "Hash is %s\n", s_hash() ? "on" : "off");

	return CMD_SUCCESS;
}

static cmdret_t
_c_help(char * cmd)
{
	static int cmdsPerLine = -1;
	int        cnt         = 0;
	int        maxCmdLen   = -1;
	int        scrSize     = 80;
	int        index       = 0;
	char       format[8];

	if (cmd)
	{
		for (index = 0; gcmdlist[index].name; index++)
		{
			if (strcasecmp(cmd, gcmdlist[index].name) == 0)
			{
				o_printf(DEBUG_ERRS_ONLY,
				       "%s\nUsage: %s%s", 
				       gcmdlist[index].desc,
				       gcmdlist[index].syntax,
				       gcmdlist[index].options ? gcmdlist[index].options: "");
				return CMD_SUCCESS;
			}
		}

		o_printf (DEBUG_ERRS_ONLY, "Unknown command %s\n", cmd);
		return CMD_ERR_BAD_CMD;
	}

	if (cmdsPerLine == -1)
	{
		/* Determine the output format */
		for (index = 0; gcmdlist[index].name; index++)
		{
			maxCmdLen = (maxCmdLen > (int)strlen(gcmdlist[index].name) ?
			               maxCmdLen : strlen(gcmdlist[index].name));
		}
		cmdsPerLine = scrSize / (5+maxCmdLen);
	}

	o_printf(DEBUG_ERRS_ONLY, 
	         "Usage \"help [topic]\" where topic is one of:\n");
	for (index = 0; gcmdlist[index].name; index++)
	{
		o_printf(DEBUG_ERRS_ONLY, gcmdlist[index].name);
		snprintf(format, 
		         8, 
		         "%%%ds", 
		         (int)((scrSize/cmdsPerLine)-strlen(gcmdlist[index].name)));
		o_printf(DEBUG_ERRS_ONLY, format, "");
		cnt++;
		if (cnt == cmdsPerLine)
		{
			cnt = 0;
			o_printf(DEBUG_ERRS_ONLY, "\n");
		}
	}
	o_printf(DEBUG_ERRS_ONLY, "\n");
	return CMD_SUCCESS;
}

static cmdret_t
_c_keepalive(int seconds)
{
	if (seconds > -1)
		s_setkeepalive(seconds);

	if (s_keepalive())
		o_printf(DEBUG_NORMAL, "Keepalive timeout set to %d\n", s_keepalive());
	else
		o_printf(DEBUG_NORMAL, "Keepalive disabled\n");
	return CMD_SUCCESS;
}

static cmdret_t
_c_link(ch_t * ch, char * oldfile, char * newfile)
{
	cmdret_t  cr = CMD_SUCCESS;
	errcode_t ec = EC_SUCCESS;

	/* Perform the link */
	C_RETRY(ec, l_link(ch->lh, oldfile, newfile));

	/* If an error occurred... */
	if (ec != EC_SUCCESS)
	{
		/* Print the error msg. */
		ec_print(ec);
		/* Destroy the error code. */
		ec_destroy(ec);
		/* Indicate that a bad command was attmpted. */
		cr = CMD_ERR_BAD_CMD;
	}

	return cr;
}

static cmdret_t
_c_list(ch_t * ch, int rflag, char * path, char * ofile)
{
	cmdret_t cr = CMD_SUCCESS;

	if (rflag && !l_supports_list(ch->lh))
	{
		o_fprintf(stderr, 
		          DEBUG_ERRS_ONLY,
		          "This service does not support recursive listing.\n");
		return CMD_ERR_BAD_CMD;
	}

	if (s_order() != ORDER_BY_NONE && !l_supports_list(ch->lh))
	{
		o_fprintf(stderr, 
		          DEBUG_ERRS_ONLY,
		          "This service does not support list ordering.\n");
		return CMD_ERR_BAD_CMD;
	}

	if (l_supports_list(ch->lh))
	{
		if (rflag || s_order() != ORDER_BY_NONE || IsGlob(path))
		{
			cr = _c_list_mlsx(ch, path, ofile, rflag);
			return cr;
		}
	}

	cr = _c_list_normal(ch, path, ofile);
	return cr;
}

static cmdret_t
_c_lscos(ch_t * ch)
{
	errcode_t   ec       = EC_SUCCESS;
	cmdret_t    cr       = CMD_SUCCESS;
	char      * cos_list = NULL;

	C_RETRY(ec, l_lscos(ch->lh, &cos_list));

	if (ec != EC_SUCCESS)
		cr = CMD_ERR_OTHER;
	else
		o_printf(DEBUG_ERRS_ONLY, "%s\n", cos_list);

	FREE(cos_list);
	ec_print(ec);
	ec_destroy(ec);

	return cr;
}

static cmdret_t
_c_lsfam(ch_t * ch)
{
	errcode_t   ec          = EC_SUCCESS;
	cmdret_t    cr          = CMD_SUCCESS;
	char      * family_list = NULL;

	C_RETRY(ec, l_lsfam(ch->lh, &family_list));

	if (ec != EC_SUCCESS)
		cr = CMD_ERR_OTHER;
	else
		o_printf(DEBUG_ERRS_ONLY, "%s\n", family_list);

	FREE(family_list);
	ec_print(ec);
	ec_destroy(ec);

	return cr;
}

static cmdret_t
_c_mkdir(ch_t * ch, char ** dirs)
{
	errcode_t ec     = EC_SUCCESS;
	cmdret_t  cr     = CMD_SUCCESS;
	char    * bname  = NULL;
	char    * dname  = NULL;
	char    * target = NULL;
	fth_t   * pfth   = NULL; /* Parent. */
	fth_t   * dfth   = NULL; /* Directory */
	ml_t    * pmlp   = NULL;
	ml_t    * dmlp   = NULL;

	for (; !ec && *dirs ; dirs++)
	{
		bname = Basename(*dirs);
		dname = Dirname(*dirs);
		pfth  = ft_init(ch->lh, dname, 0);

		while (!ec)
		{
			ec = ft_get_next_ft(pfth, &pmlp, FTH_O_ERR_NO_MATCH);
			if (!ec && !pmlp)
				break;

			if (ec)
				goto cleanup;

			if (!C_ISDIR(pmlp->type))
			{
				o_fprintf(stderr,
				          DEBUG_ERRS_ONLY,
				          "%s: Not a directory.\n",
				          pmlp->name);
				cr = CMD_ERR_BAD_CMD;
				goto cleanup;
			}

			target = MakePath(pmlp->name, bname);

			dfth = ft_init(ch->lh, target, 0);
			ec = ft_get_next_ft(dfth, &dmlp, 0);
			if (ec)
				goto cleanup;

			/* Skip remaking directories. */
			if (!dmlp || dmlp->type == 0)
				C_RETRY(ec, l_mkdir(ch->lh, target));

cleanup:
			if (ec)
				cr = CMD_ERR_DIR_OP;

			ec_print(ec);
			ec_destroy(ec);
			ml_delete(pmlp);
			ml_delete(dmlp);
			ft_destroy(dfth);
			dmlp = NULL;
			dfth = NULL;
		}
		ft_destroy(pfth);
		FREE(bname);
		FREE(dname);
	}
	return cr;
}

static cmdret_t
_c_mode(char mode)
{
	switch (mode)
	{
	case 'E':
	case 'e':
		s_seteb();
		break;
	case 'S':
	case 's':
		s_setstream();
		break;
	case '\0':
		break;
	default:
		o_fprintf(stderr, DEBUG_ERRS_ONLY, "Illegal mode %c\n", mode);
		return CMD_ERR_BAD_CMD;
	}

	if (s_stream())
		o_printf(DEBUG_NORMAL, "Stream mode\n");
	else
		o_printf(DEBUG_NORMAL, "Extended block mode\n");

	return CMD_SUCCESS;
}

static cmdret_t
_c_mget(ch_t * sch, ch_t * dch, int rflag, char ** files)
{
	cmdret_t cr     = CMD_SUCCESS;
	int      unique = 0;

	if (dch == &grch)
		unique = s_sunique();
	if (dch == &glch)
		unique = s_runique();

	for (; cr == CMD_SUCCESS && *files ; files++)
	{
		cr = _c_xfer(sch, dch, rflag, *files, NULL, unique, -1, -1);
	}
	return cr;
}

#ifdef MSSFTP
static cmdret_t
_c_open(ch_t * ch)
{
	errcode_t   ec      = EC_SUCCESS;
	cmdret_t    cr      = CMD_SUCCESS;
	int         len     = 0;
	int         ret     = 0;
	char      * buf     = NULL;
	char      * srvrmsg = NULL;
	struct passwd   pwd;
	struct passwd * pwdp  = NULL;

	do {
		len += 128;
		buf  = (char *) realloc(buf, len);

		ret  = getpwuid_r(getuid(),
		                 &pwd,
		                  buf,
		                  len,
		                 &pwdp);
	} while (ret == ERANGE);

	if (ret)
	{
		FREE(buf);
		o_fprintf(stderr, 
		          DEBUG_ERRS_ONLY,
		          "getpwuid_r failed: %s",
		          strerror(ret));
		return CMD_ERR_CONNECT;
	}

	if (!pwdp)
	{
		FREE(buf);
		o_fprintf(stderr, 
		          DEBUG_ERRS_ONLY,
		          "No username found for UID %d\n",
		          geteuid());
		return CMD_ERR_CONNECT;
	}

	C_RETRY(ec, l_connect(ch->lh, MSSHOST, 2811, pwd.pw_name, NULL, &srvrmsg));

	if (srvrmsg)
		o_fwrite(stdout, DEBUG_NORMAL, srvrmsg, strlen(srvrmsg));

	if (ec != EC_SUCCESS)
		cr = CMD_ERR_CONNECT;

	ec_print(ec);
	ec_destroy(ec);

	FREE(srvrmsg);
	FREE(buf);
	return cr;
}
#else /* MSSFTP */
static cmdret_t
_c_open(ch_t * ch, char * user, char * pass, char * host, int port)
{
	errcode_t   ec      = EC_SUCCESS;
	cmdret_t    cr      = CMD_SUCCESS;
	int         c       = 0;
	char      * srvrmsg = NULL;
	char      * spass   = NULL;
	struct termios tio;

	if (pass && !user)
	{
		o_fprintf(stderr, 
		          DEBUG_ERRS_ONLY, 
		          "username required when using passwords.\n");
		return CMD_ERR_BAD_CMD;
	}

	if (pass && strcasecmp(pass, "X") == 0)
	{
		o_printf(DEBUG_NORMAL, "Password: ");
		if (isatty(0))
		{
			memset(&tio, 0, sizeof(struct termios));
			c = tcgetattr(0, &tio);
			tio.c_lflag &= ~ECHO;

			tcsetattr(0, TCSANOW, &tio);
		}

		spass = pass = (char *) malloc(128);
		fgets(pass, 128, stdin);

		if (strlen(pass) > 0 && pass[strlen(pass) - 1] == '\n')
			pass[strlen(pass) - 1] = '\0';

		if (isatty(0))
		{
			tio.c_lflag |= ECHO;
			tcsetattr(0, TCSANOW, &tio);
		}
	}

	if (port == -1 && pass)
		port = 21;
	else if (port == -1)
		port = 2811;

	C_RETRY(ec, l_connect(ch->lh, host, port, user, pass, &srvrmsg));

	if (srvrmsg)
		o_fwrite(stdout, DEBUG_NORMAL, srvrmsg, strlen(srvrmsg));

	if (ec != EC_SUCCESS)
		cr = CMD_ERR_CONNECT;

	ec_print(ec);
	ec_destroy(ec);

	FREE(srvrmsg);
	FREE(spass);

	return cr;
}
#endif /* MSSFTP */

static cmdret_t
_c_order(char * order)
{
	if (order)
	{
		if (strcasecmp(order, "none") == 0)
		{
			s_setorder(ORDER_BY_NONE);
		}
		else if (strcasecmp(order, "name") == 0)
		{
			s_setorder(ORDER_BY_NAME);
		}
		else if (strcasecmp(order, "type") == 0)
		{
			s_setorder(ORDER_BY_TYPE);
		}
		else if (strcasecmp(order, "size") == 0)
		{
			s_setorder(ORDER_BY_SIZE);
		}else
		{
			o_fprintf(stderr, DEBUG_ERRS_ONLY, "Illegal order %s\n", order);
			return CMD_ERR_BAD_CMD;
		}
	}

	switch (s_order())
	{
	case ORDER_BY_NONE:
		o_printf(DEBUG_NORMAL, "Order set to none.\n");
		break;
	case ORDER_BY_NAME:
		o_printf(DEBUG_NORMAL, "Order set to name.\n");
		break;
	case ORDER_BY_TYPE:
		o_printf(DEBUG_NORMAL, "Order set to type.\n");
		break;
	case ORDER_BY_SIZE:
		o_printf(DEBUG_NORMAL, "Order set to size.\n");
		break;
	}

	return CMD_SUCCESS;
}

static cmdret_t
_c_parallel(int count)
{
	if (count != -1)
		s_setparallel(count);

	o_printf(DEBUG_NORMAL, 
	         "Using %d parallel data chanels for extended block transfers\n", 
	         s_parallel());

	return CMD_SUCCESS;
}

static cmdret_t
_c_passive()
{

	s_setpassive();
	o_printf(DEBUG_NORMAL, "Passive mode\n");
	return CMD_SUCCESS;
}

static cmdret_t
_c_pbsz(long long length)
{
	if (length != -1)
		s_setpbsz(length);

	if (s_pbsz())
		o_printf(DEBUG_NORMAL, 
	         "Using a %lld byte protection buffer.\n", s_pbsz());
	else
		o_printf(DEBUG_NORMAL, 
	         "Using the default protection buffer length.\n");

	return CMD_SUCCESS;
}

static cmdret_t
_c_prot(char mode)
{
	switch(mode)
	{
	case 'C':
	case 'c':
		s_setprot(0);
		break;
	case 'S':
	case 's':
		s_setprot(1);
		break;
	case 'E':
	case 'e':
		s_setprot(2);
		break;
	case 'P':
	case 'p':
		s_setprot(3);
		break;
	default:
		o_fprintf(stderr, DEBUG_ERRS_ONLY, "Illegal mode %c\n", mode);
		return CMD_ERR_BAD_CMD;
	case '\0':
		break;
	}

	switch (s_prot())
	{
	case 0:
		o_printf(DEBUG_NORMAL, "Protection set to Clear.\n");
		break;
	case 1:
		o_printf(DEBUG_NORMAL, "Protection set to Safe.\n");
		break;
	case 2:
		o_printf(DEBUG_NORMAL, "Protection set to Confidential.\n");
		break;
	case 3:
		o_printf(DEBUG_NORMAL, "Protection set to Private.\n");
		break;
	}
	return CMD_SUCCESS;
}

static cmdret_t
_c_pget(ch_t * sch, 
        ch_t * dch, 
        globus_off_t off, 
        globus_off_t len, 
        char * sfile, 
        char * dfile)
{
	return _c_xfer(sch, dch, 0, sfile, dfile, 0, off, len);
}

static cmdret_t
_c_pwd(ch_t * ch)
{
	errcode_t   ec  = EC_SUCCESS;
	cmdret_t    cr  = CMD_SUCCESS;
	char      * pwd = NULL;

	C_RETRY(ec, l_pwd(ch->lh, &pwd));

	if (ec != EC_SUCCESS)
		cr = CMD_ERR_DIR_OP;
	else
		o_printf(DEBUG_ERRS_ONLY, "%s\n", pwd);

	FREE(pwd);
	ec_print(ec);
	ec_destroy(ec);

	return cr;
}

static cmdret_t
_c_quit(ch_t * rch, ch_t * lch)
{
	if (l_connected(rch->lh))
		_c_close(rch);

	if (l_connected(lch->lh))
		_c_close(lch);

	return CMD_EXIT;
}

static cmdret_t
_c_quote(ch_t * ch, char ** words)
{
	errcode_t   ec   = EC_SUCCESS;
	cmdret_t    cr   = CMD_SUCCESS;
	char      * cmd  = NULL;
	char      * resp = NULL;

	cmd = Strdup(*words);
	for (words++; *words; words++)
	{
		cmd = Sprintf(cmd, "%s %s", cmd, *words);
	}

	C_RETRY(ec, l_quote(ch->lh, cmd, &resp));

	if (resp)
		o_printf(DEBUG_NORMAL, "%s", resp);
		

	if (ec != EC_SUCCESS)
		cr = CMD_ERR_OTHER;

	ec_print(ec);
	ec_destroy(ec);
	FREE(cmd);
	FREE(resp);

	return cr;
}

static cmdret_t
_c_rename(ch_t * ch, char * sfile, char * dfile)
{
	errcode_t  ec   = EC_SUCCESS;
	cmdret_t   cr   = CMD_SUCCESS;
	char     * bname  = NULL;
	char     * dname  = NULL;
	char     * target = NULL;
	ml_t     * smlp   = NULL;
	ml_t     * dmlp   = NULL;

	ec = _c_get_ml(ch, sfile, 0, &smlp);
	if (ec != EC_SUCCESS)
	{
		o_fprintf(stderr,
		          DEBUG_ERRS_ONLY,
		          "%s:\n",
		          sfile);
		goto finish;
	}

	ec = _c_get_ml(ch, dfile, 0, &dmlp);
	ec_destroy(ec);

	if (!dmlp)
	{
		bname = Basename(dfile);
		dname = Dirname(dfile);
		ec = _c_get_ml(ch, dname, 0, &dmlp);
		if (ec != EC_SUCCESS)
		{
			o_fprintf(stderr,
			          DEBUG_ERRS_ONLY,
			          "%s:\n",
			          dname);
			goto finish;
		}
		if (!C_ISDIR(dmlp->type))
		{
			o_fprintf(stderr,
			          DEBUG_ERRS_ONLY,
			          "%s: Not a directory\n",
			          dmlp->name);
			cr = CMD_ERR_BAD_CMD;
			goto finish;
		}
		/* Fudge */
		target = MakePath(dmlp->name, bname);
		FREE(dmlp->name);
		dmlp->name = target;
		dmlp->type = smlp->type;
	}

	if (smlp->type != dmlp->type)
	{
		o_fprintf(stderr,
		          DEBUG_ERRS_ONLY,
		          "Source and destination are different types.\n");
		cr = CMD_ERR_BAD_CMD;
		goto finish;
	}

	if (!C_ISDIR(smlp->type) && !C_ISREG(smlp->type))
	{
		o_fprintf(stderr,
		          DEBUG_ERRS_ONLY,
		          "%s: Invalid type.\n",
		          smlp->name);
		cr = CMD_ERR_BAD_CMD;
		goto finish;
	}

	if (!C_ISDIR(dmlp->type) && !C_ISREG(dmlp->type))
	{
		o_fprintf(stderr,
		          DEBUG_ERRS_ONLY,
		          "%s: Invalid type.\n",
		          dmlp->name);
		cr = CMD_ERR_BAD_CMD;
		goto finish;
	}

	C_RETRY(ec, l_rename(ch->lh, smlp->name, dmlp->name));

finish:
	if (ec != EC_SUCCESS)
		cr = CMD_ERR_RENAME;

	ec_print(ec);
	ec_destroy(ec);
	ml_delete(smlp);
	ml_delete(dmlp);
	FREE(dname);
	FREE(bname);

	return cr;
}

static cmdret_t
_c_retry(int cnt)
{
	if (cnt >= 0)
		s_setretry(cnt);

	cnt = s_retry();
	if (!cnt)
		o_printf(DEBUG_NORMAL, "Tranfer retry disabled\n");
	else
		o_printf(DEBUG_NORMAL, "Retry set to %d.\n", cnt);

	return CMD_SUCCESS;
}

static cmdret_t
_c_resume(int dflag, char * path)
{
	if (dflag)
		s_setresume(NULL);
	if (path)
		s_setresume(path);
	if (!s_resume())
		o_printf(DEBUG_NORMAL, "Resume is not set.\n");
	else
		o_printf(DEBUG_NORMAL, "%s\n", s_resume());
	return CMD_SUCCESS;
}

static cmdret_t
_c_rm(ch_t * ch, int rflag, char ** files)
{
	errcode_t   ec   = EC_SUCCESS;
	cmdret_t    cr   = CMD_SUCCESS;
	fth_t     * fth  = NULL;
	ml_t      * mlp  = NULL;
	int         opts = FTH_O_REVERSE;

	if (rflag)
		opts |= FTH_O_RECURSE;

	for (; cr == CMD_SUCCESS && *files ; files++)
	{
		fth = ft_init(ch->lh, *files, opts);

		while (1)
		{
			ec = ft_get_next_ft(fth, &mlp, FTH_O_ERR_NO_MATCH);
			if (!ec && !mlp)
				break;

			if (ec)
				goto cleanup;

			switch(mlp->type)
			{
			case S_IFDIR:
				C_RETRY(ec, l_rmdir(ch->lh, mlp->name));
				break;

			case S_IFREG:
			default:
				C_RETRY(ec, l_rm(ch->lh, mlp->name));
			}
cleanup:
			if (ec)
				cr = CMD_ERR_DELETE;

			ec_print(ec);
			ec_destroy(ec);
			ml_delete(mlp);
		}
		ft_destroy(fth);
	}

	return cr;
}

static cmdret_t
_c_rmdir(ch_t * ch, char ** dirs)
{
	errcode_t  ec  = EC_SUCCESS;
	cmdret_t   cr  = CMD_SUCCESS;
	fth_t    * fth = NULL;
	ml_t     * mlp = NULL;

	for (; *dirs; dirs++)
	{
		fth = ft_init(ch->lh, *dirs, 0);

		while (1)
		{
			ec = ft_get_next_ft(fth, &mlp, FTH_O_ERR_NO_MATCH);
			if (!ec && !mlp)
				break;

			if (ec)
				goto cleanup;

			if (!C_ISDIR(mlp->type))
			{
				o_fprintf(stderr,
				          DEBUG_ERRS_ONLY,
				          "%s: Not a directory.\n",
				          mlp->name);
				cr = CMD_ERR_BAD_CMD;
				goto cleanup;
			}
			C_RETRY(ec, l_rmdir(ch->lh, mlp->name));
cleanup:
			if (ec && mlp)
				o_fprintf(stderr, 
				          DEBUG_ERRS_ONLY,
				          "Failed to rmdir %s\n", 
				          mlp->name);

			if (ec)
				cr = CMD_ERR_DELETE;
			ec_print(ec);
			ec_destroy(ec);
			ml_delete(mlp);
		}
		ft_destroy(fth);
	}

	return cr;
}

static cmdret_t
_c_runique()
{
	s_setrunique();

	o_printf(DEBUG_NORMAL, "Receive unique is %s.\n", s_runique() ? "ON":"OFF");
	return CMD_SUCCESS;
}


static cmdret_t
_c_shell(char ** args)
{
	int       i       = 0;
	int       pid     = 0;
	int       status  = 0;
	cmdret_t  cr      = CMD_SUCCESS;
	char   *  shell   = NULL;
	char   ** pargs   = NULL;

	if ((pid = fork()) == 0)
	{
		/* Child. */
		shell = getenv("SHELL");
		if (shell == NULL)
			shell = "/bin/sh";

		pargs = (char **) malloc(4 * sizeof(char *));
		pargs[0]  = shell;
		pargs[1]  = NULL;
		pargs[2]  = NULL;
		pargs[3]  = NULL;

		if (args)
		{
			pargs[1]  = "-c";
			pargs[2]  = Strdup(args[0]);
			for (i = 1; args[i]; i++)
			{
				pargs[2] = Sprintf(pargs[2], "%s %s", pargs[2], args[i]);
			}
		}

		execv(shell, pargs);
		exit(1);
	}

	if (pid > 0)
		while (wait(&status) != pid);

	if (pid == -1)
	{
		o_fprintf(stderr,
		          DEBUG_ERRS_ONLY,
		          "Failed to launch shell process: %s\n",
		          strerror(errno));
		cr = CMD_ERR_OTHER;
	}

	return cr;
}

static cmdret_t
_c_size(ch_t * ch, char ** files)
{
	errcode_t   ec      = EC_SUCCESS;
	cmdret_t    cr      = CMD_SUCCESS;
	fth_t    *  fth     = NULL;
	ml_t     *  mlp     = NULL;
	ml_t     *  pmlp    = NULL;
	int         single  = 1;
	globus_off_t total  = 0;

	for (; *files; files++)
	{
		fth = ft_init(ch->lh, *files, 0);

		while (1)
		{
			ec = ft_get_next_ft(fth, &mlp, FTH_O_ERR_NO_MATCH);
			if (!ec && !mlp)
				break;

			if (!ec && single)
			{
				if (*(files+1) != NULL)
					single = 0;

				if (single)
				{
					ec = ft_get_next_ft(fth, &pmlp, FTH_O_PEAK);
					if (pmlp)
						single = 0;
					ml_delete(pmlp);
				}
			}

			if (!ec)
				C_RETRY(ec, l_size(ch->lh, mlp->name, &total));

			if (!ec)
				o_printf(DEBUG_ERRS_ONLY, 
				         "%s%s%"GLOBUS_OFF_T_FORMAT"\n",
				         single ? "" : mlp->name,
				         single ? "" : ": ",
				         total);

			ml_delete(mlp);

			if (ec)
			{
				ec_print(ec);
				ec_destroy(ec);
				cr = CMD_ERR_OTHER;
			}
		}
		ft_destroy(fth);
	}
	return cr;
}

static cmdret_t
_c_stage(ch_t * ch, int rflag, int t, char ** files)
{
	errcode_t   ec      = EC_SUCCESS;
	cmdret_t    cr      = CMD_SUCCESS;
	fth_t    *  fth     = NULL;
	ml_t     ** mlp     = NULL;
	time_t      start   = 0;
	int         staged  = 0;
	int         allstaged = 0;
	int         ind       = 0;
	int         cnt       = 0;
	int         opts      = 0;

	if (rflag)
		opts = FTH_O_RECURSE;

	for (; *files; files++)
	{
		fth = ft_init(ch->lh, *files, opts);

		while (1)
		{
			mlp = (ml_t **) realloc(mlp, sizeof(ml_t*) * (cnt + 1));
			ec = ft_get_next_ft(fth, &mlp[cnt], FTH_O_ERR_NO_MATCH);
			if (!ec && !mlp[cnt])
				break;

			if (!ec && C_ISREG(mlp[cnt]->type))
			{
				ec = l_stage(ch->lh, mlp[cnt]->name, &staged);

				if (!ec && staged)
					o_printf(DEBUG_NORMAL, "%s: Success\n", mlp[cnt]->name);
			}

			/*
			 * If ...
			 *   An error occurred
			 *   This is not a regular file
			 *   This file has staged
			 * Then delete this entry
			 */
			if (ec || !C_ISREG(mlp[cnt]->type) || staged) 
				ml_delete(mlp[cnt]);
			else
				cnt++;

			if (ec)
			{
				ec_print(ec);
				ec_destroy(ec);
				cr = CMD_ERR_GET;
			}
		}
		ft_destroy(fth);
	}

	start = time(NULL);
	while (!allstaged)
	{
		allstaged = 1;

		for (ind = 0; ind < cnt; ind++)
		{
			if (!mlp[ind])
				continue;

			ec = l_stage(ch->lh, mlp[ind]->name, &staged);

			if (!ec && staged)
				o_printf(DEBUG_NORMAL, "%s: Success\n", mlp[ind]->name);

			if (ec || staged)
			{
				ml_delete(mlp[ind]);
				mlp[ind] = NULL;
			}

			if (!ec && !staged)
				allstaged = 0;

			if (ec)
			{
				ec_print(ec);
				ec_destroy(ec);
				cr = CMD_ERR_GET;
			}
		}

		/* Break if we have waited the requested length of time. */
		if ((time(NULL) - start) > t)
			break;

		/* Sleep for a second before we try again. */
		sleep(1);
	}

	for (ind = 0; ind < cnt; ind++)
	{
		if (!mlp[ind])
			continue;

		o_printf(DEBUG_NORMAL, "%s: Staging\n", mlp[ind]->name);
		ml_delete(mlp[ind]);
	}
	FREE(mlp);
	return cr;
}

static cmdret_t
_c_sunique()
{
	s_setsunique();

	o_printf(DEBUG_NORMAL, "Store unique is %s.\n", s_sunique() ? "ON":"OFF");
	return CMD_SUCCESS;
}

static cmdret_t
_c_symlink(ch_t * ch, char * oldfile, char * newfile)
{
	cmdret_t  cr = CMD_SUCCESS;
	errcode_t ec = EC_SUCCESS;

	/* Perform the link */
	C_RETRY(ec, l_symlink(ch->lh, oldfile, newfile));

	/* If an error occurred... */
	if (ec != EC_SUCCESS)
	{
		/* Print the error msg. */
		ec_print(ec);
		/* Destroy the error code. */
		ec_destroy(ec);
		/* Indicate that a bad command was attmpted. */
		cr = CMD_ERR_BAD_CMD;
	}

	return cr;
}

static cmdret_t
_c_tcpbuf(long long size)
{
	if (size != -1)
		s_settcpbuf(size);

	size = s_tcpbuf();

	if (size == 0)
		o_printf(DEBUG_NORMAL, "TCP buffer set to system default\n");
	else
		o_printf(DEBUG_NORMAL, "TCP buffer set to %lld bytes\n", size);

	return CMD_SUCCESS;
}

#ifdef MSSFTP
static cmdret_t
_c_type(char * type)
{
	if (type && strcasecmp(type, "ascii") == 0)
		s_setascii();
	if (type && strcasecmp(type, "binary") == 0)
		s_setbinary();

	o_printf(DEBUG_NORMAL, 
	         "Transfer type set to %s\n",
	         s_ascii() ? "ASCII" : "BINARY");

	return CMD_SUCCESS;
}
#endif /* MSSFTP */

static cmdret_t
_c_versions()
{
	globus_module_print_activated_versions(stdout, GLOBUS_TRUE);
	return CMD_SUCCESS;
}

static cmdret_t
_c_wait()
{
	s_setwait();
	o_printf(DEBUG_NORMAL, "WAIT is %s.\n", s_wait() ? "enabled" : "disabled");
	return CMD_SUCCESS;
}

static errcode_t
_c_get_ml(ch_t * ch, char * target, int type, ml_t ** mlp)
{
	fth_t  *  fth  = NULL;
	ml_t   *  tmlp = NULL;
	errcode_t ec  = EC_SUCCESS;

	*mlp = NULL;
	fth = ft_init(ch->lh, target, 0);
	ec  = ft_get_next_ft(fth, mlp, FTH_O_ERR_NO_MATCH);
	if (ec != EC_SUCCESS || !*mlp)
		goto cleanup;

	ec  = ft_get_next_ft(fth, &tmlp, FTH_O_PEAK);

	if (!ec && tmlp)
		ec = ec_create(EC_GSI_SUCCESS,
		               EC_GSI_SUCCESS,
		               "Too many matches for %s.",
		               target);

	if (ec)
		goto cleanup;

	switch ((*mlp)->type)
	{
	case S_IFREG:
		if (type && type != S_IFREG)
			ec = ec_create(EC_GSI_SUCCESS,
			               EC_GSI_SUCCESS,
				           "%s: not a directory",
				           (*mlp)->name);
		break;

	case S_IFDIR:
		if (type && type != S_IFDIR)
			ec = ec_create(EC_GSI_SUCCESS,
			               EC_GSI_SUCCESS,
				           "%s: not a file",
				           (*mlp)->name);
		break;
	case 0:
		(*mlp)->type = type;
		goto cleanup;
	}

cleanup:
	if (ec)
	{
		ml_delete(*mlp);
		ml_delete(tmlp);
		*mlp = NULL;
	}

	ft_destroy(fth);
	return ec;
}

static cmdret_t
_c_utime(ch_t * Channel,
         char * Target,
         ml_t * Reference)
{
	errcode_t ec = EC_SUCCESS;
	cmdret_t  cr = CMD_SUCCESS;

	/*
	 * Check that our reference has a valid time stamp.
	 */
	if (Reference->mf.Modify == 0)
		return CMD_SUCCESS;

	/*
	 * Now update the timestamp. The underlying protocol may not
	 * be capable but that is ok. I expect the underlying protocol to
	 * just return success if it doesn't support it.
	 */
	C_RETRY(ec, l_utime(Channel->lh, Target, Reference->modify));
	
	if (ec)
		cr = CMD_ERR_GET;

	ec_print(ec);
	ec_destroy(ec);

	return cr;
}

static cmdret_t
_c_xfer(ch_t       * sch, 
        ch_t       * dch, 
        int          rflag, 
        char       * sfile, 
        char       * dfile, 
        int          unique, 
        globus_off_t soff, 
        globus_off_t slen)
{
	errcode_t ec     = EC_SUCCESS;
	cmdret_t  lcr    = CMD_SUCCESS;
	cmdret_t  cr     = CMD_SUCCESS;
	fth_t   * sfth   = NULL; /* Source. */
	fth_t   * dfth   = NULL; /* Destination. */
	fth_t   * pfth   = NULL; /* Parent. */
	char    * target = NULL;
	char    * bname  = NULL; /* basename */
	char    * dname  = NULL; /* dirname  */
	char    * ppath  = NULL;
	char    * tstr   = NULL;
	ml_t    * dmlp   = NULL; /* dst ml_t */
	ml_t    * smlp   = NULL; /* src ml_t */
	ml_t    * pmlp   = NULL; /* dst parent ml_t */
	ml_t    * tmlp   = NULL;
	int       msrcs  = 0;
	int       opts   = 0;
	char    * dirs[2];

	/* Determine the source object(s) and type(s). */
	sfth = ft_init(sch->lh, sfile, opts);
	ec = ft_get_next_ft(sfth, &smlp, FTH_O_ERR_NO_MATCH);
	if (ec)
		goto finish;

	ec = ft_get_next_ft(sfth, &tmlp, FTH_O_PEAK);
	if (ec)
		goto finish;

	if (tmlp)
		msrcs = 1;
	ml_delete(tmlp);

	/* Reopen if recursive. We do this to correctly identify msrcs above. */
	if (rflag)
	{
		opts = FTH_O_RECURSE;
		ml_delete(smlp);
		ft_destroy(sfth);

		sfth = ft_init(sch->lh, sfile, opts);
		ec = ft_get_next_ft(sfth, &smlp, FTH_O_ERR_NO_MATCH);
		if (ec)
			goto finish;
	}

	if (smlp->type == 0)
	{
		if (rflag)
			smlp->type = S_IFDIR;
		else
			smlp->type = S_IFREG;
	}

	/* Determine the destination object and type. */
	if (dfile)
	{
		dfth = ft_init(dch->lh, dfile, 0);
		/* Can't error on no match because there is no match on file, file */
		ec = ft_get_next_ft(dfth, &dmlp, 0);
		if (ec)
			goto finish;

		if (dmlp)
		{
			ec = ft_get_next_ft(dfth, &tmlp, FTH_O_PEAK);
			if (ec)
				goto finish;

			if (tmlp)
			{
				o_fprintf(stderr,
				          DEBUG_ERRS_ONLY,
				          "Too many matches for %s\n",
				          dfile);
				cr = CMD_ERR_BAD_CMD;
				ml_delete(tmlp);
				goto finish;
			}

			if (dmlp->type == S_IFREG)
			{
				if (smlp->type == S_IFDIR || msrcs || rflag)
				{
					o_fprintf(stderr,
					          DEBUG_ERRS_ONLY,
					         "Destination must be a directory.\n",
					          dfile);
					          cr = CMD_ERR_BAD_CMD;
					          goto finish;
				}
			}
		}
	}

	/* If no match on dfile, determine the parent object and type. */
	if (dfile && !dmlp)
	{
		bname = Basename(dfile);
		dname = Dirname(dfile);

		pfth  = ft_init(dch->lh, dname, 0);
		ec = ft_get_next_ft(pfth, &pmlp, FTH_O_ERR_NO_MATCH);
		if (ec)
			goto finish;

		ec = ft_get_next_ft(pfth, &tmlp, FTH_O_PEAK);
		if (ec)
			goto finish;

		if (tmlp)
		{
			o_fprintf(stderr,
			          DEBUG_ERRS_ONLY,
			          "Too many matches for %s\n",
			          dname);
			cr = CMD_ERR_BAD_CMD;
			ml_delete(tmlp);
			goto finish;
		}
		/* pmlp->type will NOT be unknown. */
		if (pmlp->type != S_IFDIR)
		{
			o_fprintf(stderr,
			          DEBUG_ERRS_ONLY,
			          "%s is not a directory\n",
			          dname);
			cr = CMD_ERR_BAD_CMD;
			goto finish;
		}

		if (msrcs)
		{
			o_fprintf(stderr,
			          DEBUG_ERRS_ONLY,
			          "%s must exist when given multiple sources.\n",
			          dfile);
			cr = CMD_ERR_BAD_CMD;
			goto finish;
		}
		ppath = MakePath(pmlp->name, bname);
	}

	do
	{
		if (pmlp)
			target = MakePath(ppath, PathMinusRoot(smlp->name, sfile));

		if (dmlp)
		{
			switch (smlp->type)
			{
			case 0:
			case S_IFREG:
				/*
				 * Allow us to map a regular file to:
				 *  (1) a regular file if this is a single file transfer or
				 *  (2) a device (like /dev/null)
				 */
				if ((C_ISREG(dmlp->type) && !rflag && !msrcs) ||
				    (S_ISCHR(dmlp->type) || S_ISBLK(dmlp->type)))
				{
					target = Strdup(dmlp->name);
					break;
				}
				/* Fall through */
			case S_IFDIR:
/* Is PathMinusRoot() thrown off by ending slashes? */
				if (msrcs)
					tstr = MakePath(Basename(smlp->name), 
					                PathMinusRoot(smlp->name, sfile));
				else
					tstr = MakePath(Basename(sfile), 
					                PathMinusRoot(smlp->name, sfile));
				target = MakePath(dmlp->name, tstr);
				FREE(tstr);
				break;

			default:
				ml_delete(smlp);
				smlp = NULL;
				continue;
			}
		}

		if (!dfile)
			target = strdup(smlp->name);

		if (rflag && s_resume())
		{
			if (strcmp(target, s_resume()))
			{
				FREE(target);
				ml_delete(smlp);
				smlp = NULL;
				continue;
			}
			s_setresume(NULL);
		}

		switch (smlp->type)
		{
		case 0:
		case S_IFREG:
			lcr = _c_xfer_file(sch, dch, smlp->name, target, unique, soff, slen);

			/* If the transfer was successful, update the timestamp. */
			if (lcr == CMD_SUCCESS)
				_c_utime(dch, target, smlp);

			/* Update cr with any local error. */
			cr |= lcr;
			break;

		case S_IFDIR:
			dirs[0] = target;
			dirs[1] = NULL;
			cr |= _c_mkdir(dch, dirs);
			break;
		}

		FREE(target);
		ml_delete(smlp);
		smlp = NULL;
	} while (!(ec = ft_get_next_ft(sfth, &smlp, 0)) && smlp);

finish:
	if (ec)
		cr = CMD_ERR_GET;
	ec_print(ec);
	ec_destroy(ec);
	ml_delete(dmlp);
	ml_delete(smlp);
	ml_delete(pmlp);
	ft_destroy(sfth);
	ft_destroy(dfth);
	ft_destroy(pfth);
	FREE(dname);
	FREE(bname);
	FREE(target);
	FREE(ppath);
	return cr;
}

static cmdret_t
_c_xfer_file(ch_t * sch,
             ch_t * dch,
             char * src,
             char * dst,
             int    unique,
             globus_off_t soff, 
             globus_off_t slen)
{
	errcode_t ec  = EC_SUCCESS;
	errcode_t ecr = EC_SUCCESS;
	errcode_t ecl = EC_SUCCESS;
	cmdret_t  cr  = CMD_SUCCESS;
	size_t          hashlen = 0;
	int             delfile = 0;
	int             hashnl  = 0;
	int             staged  = 0;
	int             retry   = s_retry();
	int             eof     = 0;
	int             supported = 0;
	char          * buf     = NULL;
	char          * tim     = NULL;
	char          * rate    = NULL;
	struct timeval  start;
	struct timeval  stop;
	size_t          len     = 0;
	globus_off_t    off     = 0;
	globus_off_t    total   = 0;
	unsigned int    lcrc    = 0;
	unsigned int    rcrc    = 0;

	/* Try to get the size of the remote file. */
	if (slen == (globus_off_t)-1)
	{
		C_RETRY(ec, l_size(sch->lh, src, &slen));
		ec_destroy(ec);
		ec = EC_SUCCESS;
	}

	/* Stage it */
	do
	{
		ec = l_stage(sch->lh, src, &staged);
		if (!s_wait())
			break;

		if (!ec && !staged)
			sleep(15);
	} while (!staged && !ec);

	if (ec)
	{
		o_fprintf(stderr,
		          DEBUG_ERRS_ONLY,
		          "%s: Failed to stage file.\n",
		          src);
		ec_print(ec);
		ec_destroy(ec);
		return CMD_ERR_GET;
	}

	if (!staged)
	{
		o_fprintf(stderr, 
		          DEBUG_ERRS_ONLY,
		          "%s is being retrieved from the archive. Please retry the transfer once the file is staged or use the wait command.\n",
		          src);
		return CMD_ERR_GET;
	}

	/* Transfer it */
	while (1)
	{
		if (retry < s_retry())
			o_fprintf(stderr,
			          DEBUG_ERRS_ONLY,
			          "%s: Transfer failed, retrying.\n",
			          src);

		ec_destroy(ec);
		ec_destroy(ecl);
		ec_destroy(ecr);
		ec = ecl = ecr = NULL;
		eof = 0;

		ec = l_storfile(dch->lh, sch->lh, dst, unique, soff, slen);

		if (ec != EC_SUCCESS)
		{
			o_fprintf(stderr,
			          DEBUG_ERRS_ONLY,
			          "%s: Failed to store file.\n",
			          dst);
			goto cleanup;
		}

		if (soff == (globus_off_t)-1)
			delfile = 1;

		ec = l_retrvfile(sch->lh, dch->lh, src, soff, slen);

		if (ec != EC_SUCCESS)
		{
			o_fprintf(stderr,
          			DEBUG_ERRS_ONLY,
          			"%s: Failed to retrieve file.\n",
          			src);
			goto cleanup;
		}

		gettimeofday(&start, NULL);
		while (!eof)
		{
			ec = l_read(sch->lh, &buf, &off, &len, &eof);

			if (ec)
			{
				if (hashnl)
				{
					fputc((int)'\n', stdout);
					hashnl = 0;
				}
				o_fprintf(stderr,
				          DEBUG_ERRS_ONLY,
				          "%s: Error reading from source.\n",
				          src);
				break;
			}

			ec = l_write(dch->lh, buf, off, len, eof);

			if (ec)
			{
				if (hashnl)
				{
					fputc((int)'\n', stdout);
					hashnl = 0;
				}
				o_fprintf(stderr,
				          DEBUG_ERRS_ONLY,
				          "%s: Error writing to destination.\n",
				          dst);
				break;
			}
			total += len;

			if (s_hash())
			{
				if ((hashlen + len) >= (1024*1024))
				{
					fputc((int)'#', stdout);
					fflush(stdout);
					hashnl = 1;
				}
				hashlen = (hashlen + len) % (1024 * 1024);
			}
		}
		gettimeofday(&stop, NULL);

		if (hashnl)
		{
			fputc((int)'\n', stdout);
			hashnl = 0;
		}

cleanup:
		ecr = l_close(dch->lh);
		ecl = l_close(sch->lh);

		if (!ec_retry(ecl) && !ec_retry(ecr) && !ec_retry(ec))
			break;

		if (s_retry() && retry-- <= 0)
			break;
	}

	if (ecl || ecr || ec)
		cr = CMD_ERR_GET;

	ec_print(ec);
	ec_destroy(ec);

	if (ecl)
		o_fprintf(stderr,
		          DEBUG_ERRS_ONLY,
		          "%s: Error with local service during transfer.\n",
		          src);
	ec_print(ecl);
	ec_destroy(ecl);

	if (ecr)
		o_fprintf(stderr,
		          DEBUG_ERRS_ONLY,
		          "%s: Error with remote service during transfer.\n",
		          dst);
	ec_print(ecr);
	ec_destroy(ecr);

	/* Remove the destination on error. */
	if (cr != CMD_SUCCESS && delfile)
	{
		ec = l_rm(dch->lh, dst);
		if (ec)
			o_fprintf(stderr,
			          DEBUG_ERRS_ONLY,
			          "Failed to remove the destination file.\n");

		ec_print(ec);
		ec_destroy(ec);
	}

	if (cr == CMD_SUCCESS)
	{
#ifdef NOT
		/*
		 * I'm not sure under what circumstances 'total' would be zero given
		 * the code above. But I do know it is breaking the report for
		 *   pput 0 0 file
		 */
		if (total == 0)
		{
			if (slen != -1)
				total = slen;
		}

		if (total == 0)
		{
			C_RETRY(ec, l_size(sch->lh, src, &total));
			if (ec)
			{
				ec_destroy(ec);
				C_RETRY(ec, l_size(dch->lh, dst, &total));
			}
			ec_destroy(ec);
		}
#endif /* NOT */
		buf   = Sprintf(NULL, "%"GLOBUS_OFF_T_FORMAT" bytes", total);
		tim   = Convtime(&start, &stop);
		rate  = MkRate(&start, &stop, total);

		o_fprintf(stdout,
		          DEBUG_NORMAL,
		          "%s: %s in %s%s%s%s\n",
		          *src == '|' ? dst : src,
		          buf,
		          tim,
		          rate  ? " ("  : "",
		          rate  ? rate : "",
		          rate  ? ")"  : "");

		FREE(buf);
		FREE(tim);
		FREE(rate);
	}

#ifdef SYSLOG_PERF
	if (cr == CMD_SUCCESS)
		record_perf(sch->lh, dch->lh, src, dst, total);
#endif /* SYSLOG_PERF */

	if (cr == CMD_SUCCESS && s_cksum() && *dst != '|' && *src != '|')
	{
		C_RETRY(ec, l_cksum(sch->lh, src, &supported, &lcrc));
		if (ec)
		{
			o_fprintf(stderr, DEBUG_ERRS_ONLY, "Failed to sum local file\n");
			ec_print(ec);
			ec_destroy(ec);
			return CMD_ERR_OTHER;
		}
		if (!supported)
		{
			o_fprintf(stderr, 
			          DEBUG_ERRS_ONLY,
			          "The local service does not support cksum\n");
			return CMD_ERR_BAD_CMD;
		}
		C_RETRY(ec, l_cksum(dch->lh, dst, &supported, &rcrc));
		if (ec)
		{
			o_fprintf(stderr, DEBUG_ERRS_ONLY, "Failed to sum remote file\n");
			ec_print(ec);
			ec_destroy(ec);
			return CMD_ERR_OTHER;
		}
		if (!supported)
		{
			o_fprintf(stderr, 
			          DEBUG_ERRS_ONLY, 
			          "The remote service does not support cksum\n");
			return CMD_ERR_BAD_CMD;
		}

		if (lcrc != rcrc)
		{
			o_fprintf(stderr, 
			          DEBUG_ERRS_ONLY,
			          "The local and remote cksum do not match.\n");
			return CMD_ERR_OTHER;
		}
	}

	return cr;
}

static cmdret_t
_c_list_mlsx(ch_t * ch, char * path, char * ofile, int rflag)
{
	errcode_t ec    = EC_SUCCESS;
	cmdret_t  cr    = CMD_SUCCESS;
	mlrs_t  * mlrs  = NULL;
	fth_t   * fth   = NULL;
	ml_t    * mlp   = NULL;
	int       perms = 0;
	int       opts  = 0;
	char    * mode  = NULL;
	char    * tstr  = NULL;
	FILE    * outf  = stdout;

	if (s_order() != ORDER_BY_NONE)
		ml_init(&mlrs);

	if (rflag)
		opts |= FTH_O_RECURSE;
	opts |= FTH_O_EXPAND_DIR;

	if (ofile && strcmp(ofile, "-") != 0)
	{
		outf = fopen(ofile, "w");
		if (!outf)
		{
			ec = ec_create(EC_GSI_SUCCESS,
			               EC_GSI_SUCCESS,
			               "Could not open %s: %s.",
			               ofile, 
			               strerror(errno));
			goto cleanup;
		}
	}

	fth = ft_init(ch->lh, path ? path : ".", opts);
	while (1)
	{
		ec = ft_get_next_ft(fth, &mlp, FTH_O_ERR_NO_MATCH);
		if (ec)
		{
			ec_print(ec);
			ec_destroy(ec);
			ec = EC_SUCCESS;
			cr = CMD_ERR_OTHER;
			continue;
		}

		if (s_order() == ORDER_BY_NONE && !mlp)
			break;

		if (s_order() != ORDER_BY_NONE)
		{
			if (mlp)
			{
				ec = ml_store_rec(mlrs, mlp);
				if (ec)
					break;
				continue;
			}
			ec = ml_fetch_rec(mlrs, &mlp);
			if (ec)
				break;
			if (!mlp)
				break;
		}

		if (mlp->mf.UNIX_mode)
		{
			perms = strtol(mlp->UNIX_mode, NULL, 8);
			mode  = UnixPermStr(perms | mlp->type);
			fprintf(outf, "%s  ", mode);
			FREE(mode);
		}
		if (mlp->mf.UNIX_owner)
			fprintf(outf, "%-8s ", mlp->UNIX_owner);
		if (mlp->mf.UNIX_group)
			fprintf(outf, "%-9s ", mlp->UNIX_group);
		if (mlp->mf.X_archive)
			fprintf(outf, "%-3s ", mlp->X_archive);
		if (mlp->mf.X_family)
			fprintf(outf, "%-7s ", mlp->X_family);
		if (mlp->mf.Size)
			fprintf(outf, "%13"GLOBUS_OFF_T_FORMAT"  ", mlp->size);
		tstr = ctime(&mlp->modify);
		if (mlp->mf.Modify)
			fprintf(outf, "%-12.12s  ", tstr + 4);
		fprintf(outf, "%s", mlp->name);
		if (mlp->mf.UNIX_slink)
			fprintf(outf, " -> %s", mlp->UNIX_slink);
		fprintf(outf, "\n");
		ml_delete(mlp);
		mlp = NULL;
	}

cleanup:
	ec_print(ec);
	ec_destroy(ec);

	ml_destroy(mlrs);
	ft_destroy(fth);

	if (outf && outf != stdout)
		fclose(outf);

	if (ec)
		return CMD_ERR_OTHER;
	return cr;
}

static cmdret_t
_c_list_normal(ch_t * ch, char * path, char * ofile)
{
	errcode_t       ec    = EC_SUCCESS;
	errcode_t       ecl   = EC_SUCCESS;
	int             eof   = 0;
	int             retry = s_retry();
	globus_off_t    off   = 0;
	size_t          len   = 0;
	char          * buf   = NULL;
	ml_t          * mlp   = NULL;
	fth_t         * fth   = NULL;
	FILE          * outf  = stdout;

	if (ofile && strcmp(ofile, "-") != 0)
	{
		outf = fopen(ofile, "w");
		if (!outf)
		{
			ec = ec_create(EC_GSI_SUCCESS,
			               EC_GSI_SUCCESS,
			               "Could not open %s: %s.",
			               ofile,
			               strerror(errno));
			goto cleanup;
		}
	}

	/* Fix for no match */
	if (path)
	{
		fth = ft_init(ch->lh, path, 0);
		ec = ft_get_next_ft(fth, &mlp, FTH_O_ERR_NO_MATCH);
		if (ec)
			goto cleanup;
	}

retry:
	ec = l_list(ch->lh, path);

	if (ec)
		goto cleanup;

	while (ec == EC_SUCCESS && !eof)
	{
		ec = l_read(ch->lh, &buf, &off, &len, &eof);
		if (!ec && buf)
			o_fwrite(outf, DEBUG_ERRS_ONLY, buf, len);
		FREE(buf);
	}

cleanup:
	ecl = l_close(ch->lh);
	if ((ec_retry(ec) || ec_retry(ecl)) && (retry-- > 0 || !s_retry()))
		goto retry;

	ec_print(ec);
	ec_destroy(ec);
	ec_print(ecl);
	ec_destroy(ecl);
	ml_delete(mlp);
	ft_destroy(fth);

	if (outf && outf != stdout)
		fclose(outf);

	if (ec || ecl)
		return CMD_ERR_OTHER;
	return CMD_SUCCESS;
}

#ifdef CMDS2MAN
int
main()
{
	char * lasts = NULL;
	char * cptr  = NULL;
	char * sptr  = NULL;
	char * tok   = NULL;
	int    i     = 0;

	printf(".TH UBERFTP 1C \"16 May 2008\"\n");
	printf(".SH COMMANDS\n");

	for (i = 0; gcmdlist[i].name != NULL; i++)
	{
		printf(".TP\n.B %s", gcmdlist[i].syntax);
		printf(gcmdlist[i].desc);

		if (gcmdlist[i].options)
		{
			/* printf(".br\n.br\nOptions:\n"); */
			sptr = cptr = Strdup(gcmdlist[i].options);

			while ((tok = strtok_r(cptr, "\n", &lasts)))
			{
				cptr = NULL;
				printf(".br\n%s\n", tok);
			}

			FREE(sptr);
		}
	}
	return 0;
}
#endif /* CMDS2MAN */

