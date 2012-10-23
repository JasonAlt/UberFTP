dnl
dnl  University of Illinois/NCSA Open Source License
dnl
dnl  Copyright © 2003-2012, NCSA.  All rights reserved.
dnl
dnl  Developed by:
dnl
dnl  Storage Enabling Technologies (SET)
dnl
dnl  Nation Center for Supercomputing Applications (NCSA)
dnl
dnl  http://dims.ncsa.uiuc.edu/set/uberftp
dnl
dnl  Permission is hereby granted, free of charge, to any person obtaining a
dnl  copy of this software and associated documentation files (the .Software.),
dnl  to deal with the Software without restriction, including without limitation
dnl  the rights to use, copy, modify, merge, publish, distribute, sublicense,
dnl  and/or sell copies of the Software, and to permit persons to whom the
dnl  Software is furnished to do so, subject to the following conditions:
dnl
dnl     + Redistributions of source code must retain the above copyright notice,
dnl       this list of conditions and the following disclaimers.
dnl
dnl     + Redistributions in binary form must reproduce the above copyright
dnl       notice, this list of conditions and the following disclaimers in the
dnl       documentation and/or other materials provided with the distribution.
dnl
dnl     + Neither the names of SET, NCSA
dnl       nor the names of its contributors may be used to endorse or promote
dnl       products derived from this Software without specific prior written
dnl       permission.
dnl
dnl  THE SOFTWARE IS PROVIDED .AS IS., WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
dnl  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
dnl  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
dnl  THE CONTRIBUTORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
dnl  OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
dnl  ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
dnl  DEALINGS WITH THE SOFTWARE.
dnl

dnl Process this file with autoconf to produce a configure script.
# First [] is used as the prompt
# Last [] is used for the version information
AC_INIT(UberFTP (2.7 BETA 5), [2.7_BETA_5], [Jason Alt <jalt@ncsa.illinois.edu>],[uberftp-client])
AM_INIT_AUTOMAKE 

GPT_MAJOR="2"
GPT_MINOR="7"
AC_SUBST(GPT_MAJOR)
AC_SUBST(GPT_MINOR)
AC_OUTPUT(pkg_data_src.gpt)

# Check for an MSSFTP build
AC_ARG_ENABLE(mssftp,
	[  --enable-mssftp                Enable mssftp support ],
	[  mssftp="$enableval" ],
	[  mssftp="no" ]
)

if test "x$mssftp" = "xyes" ; then
	AC_DEFINE(MSSFTP, 1, [ Whether to build in mssftp functionality ])
fi

AC_ARG_ENABLE(msshost,
	[  --enable-msshost=HOSTNAME      Set the MSS host to connect to ],
	[  msshost="$enableval"  ],
	[  msshost="mss.ncsa.uiuc.edu"  ]
)
AC_DEFINE_UNQUOTED(MSSHOST, "$msshost", [ MSS to connect to ])

AC_ARG_ENABLE(mssftp-gsi-service,
	[  --enable-mssftp-gsi-service    Mssftp will use a GSI service cert ],
	[  msssrvc="$enableval" ],
	[  msssrvc="no" ]
)

if test "x$msssrvc" = "xyes" ; then
	AC_DEFINE(MSSFTP_GSI_SERVICE, 1, [ Mssftp will use a GSI service cert ])
fi

AC_ARG_ENABLE(static-build,
	[  --enable-static-build          Build statically ],
	[  staticbuild="$enableval"  ],
	[  staticbuild="no"  ]
)
linkflag=""
if test "x$staticbuild" = "xyes" ; then
	linkflag="--static"
fi

# Check whether the user has a Globus flavor type
globus_flavor="no"
AC_ARG_WITH(globus-flavor,
	[  --with-globus-flavor=TYPE       Describe Globus flavor type (ex: gcc32dbg)],
	[ globus_flavor="$withval" ]
)

if test "x$globus_flavor" = "xyes" ; then
	AC_MSG_ERROR(--with-globus-flavor=TYPE must specify a flavor)
fi
if test "x$globus_flavor" = "xno" ; then
	AC_MSG_ERROR(--with-globus-flavor=TYPE must specify a flavor)
fi

# Find GLOBUS_LOCATION
globus_location="no"
AC_ARG_WITH(globus,
	[  --with-globus=PATH      Location of Globus Installation],
	[
		globus_location="$withval"
	]
)

if test "x$globus_location" = "xyes" ; then
	AC_MSG_ERROR(--with-globus=PATH must specify a path)
elif test "x$globus_location" = "xno" ; then
	if test "x$GLOBUS_LOCATION" = "x" ; then
		AC_MSG_ERROR(--with-globus=PATH must specify a path)
	else
		globus_location="$GLOBUS_LOCATION"
	fi
fi

if test ! -d "$globus_location" ; then
	AC_MSG_ERROR(--with-globus=PATH must specify a valid path)
fi
export GLOBUS_LOCATION=$globus_location

# Find GPT_LOCATION
gpt_location="no"
AC_ARG_WITH(gpt,
	[  --with-gpt=PATH         Location of GPT Installation],
	[
		gpt_location="$withval"
	]
)
if test "x$gpt_location" = "xyes" ; then
	AC_MSG_ERROR(--with-gpt=PATH must specify a path)
elif test "x$gpt_location" = "xno" ; then
	if test -x $globus_location/sbin/gpt-build ; then
		gpt_location=$globus_location
	elif test "x$GPT_LOCATION" != "x" ; then
		gpt_location=$GPT_LOCATION
	fi
fi

if test ! -d "$gpt_location" ; then
	AC_MSG_ERROR(--with-gpt=PATH must specify a valid path $gpt_location)
fi
export GPT_LOCATION=$gpt_location

# Default TPC Window Sizing
AC_ARG_WITH(tcp-window,
	[  --with-tcp-window=value      Default value for TCP Windows ],
	[ default_tcp_window="$withval" ],
	[ default_tcp_window=0 ]
)
AC_DEFINE_UNQUOTED(DEFAULT_TCP_BUFFER_SIZE, $default_tcp_window, [ Setting the default TCP window settings ])

# Default Internal Buffer sizing
AC_ARG_WITH(blksize,
	[  --with-blksize=value      Default size of internal buffers ],
	[ default_blksize="$withval" ],
	[ default_blksize=1048576 ]
)
AC_DEFINE_UNQUOTED(DEFAULT_BLKSIZE, $default_blksize, [ Setting the default buffer size ])

#
# Find globus-makefile-header so we can build our cflags, ldflags, compiler choice, etc
#
AC_MSG_CHECKING(for globus-makefile-header)
if test -x $gpt_location/bin/globus-makefile-header ; then
	globus_makefile_header="$gpt_location/bin/globus-makefile-header"
else
	AC_MSG_ERROR(Cannot find globus-makefile-header: GPT installation is incomplete)
fi
AC_MSG_RESULT(yes)


#
# Now run globus-makefile-header
#
echo "${globus_makefile_header} --flavor=${globus_flavor} $linkflag globus_gssapi_gsi"
${globus_makefile_header} --flavor=${globus_flavor} $linkflag globus_gssapi_gsi 2>/dev/null | sed 's/ = \(.*\)/="\1"/' > ./.globus_makefile_header.output
. ./.globus_makefile_header.output
rm -f ./.globus_makefile_header.output

AC_MSG_CHECKING(for Globus compiler choice)
if test -z "$GLOBUS_CC" ; then
	AC_MSG_ERROR(GLOBUS_CC missing)
fi
AC_MSG_RESULT(yes)

AC_MSG_CHECKING(for Globus cflags)
if test -z "$GLOBUS_CFLAGS" ; then
	AC_MSG_ERROR(GLOBUS_CFLAGS missing)
fi
AC_MSG_RESULT(yes)

AC_MSG_CHECKING(for Globus includes)
if test -z "$GLOBUS_INCLUDES" ; then
	AC_MSG_ERROR(GLOBUS_INCLUDES missing)
fi
AC_MSG_RESULT(yes)

AC_MSG_CHECKING(for Globus libraries)
if test -z "$GLOBUS_PKG_LIBS" ; then
	AC_MSG_ERROR(GLOBUS_PKG_LIBS missing)
fi
AC_MSG_RESULT(yes)

AC_MSG_CHECKING(for Globus ldflags)
if test -z "$GLOBUS_LDFLAGS" ; then
	AC_MSG_ERROR(GLOBUS_LDFLAGS missing)
fi
AC_MSG_RESULT(yes)

# Set our compiler choice
CC=$GLOBUS_CC
AC_PROG_CC()

# Tru64 fix
AC_CHECK_FUNCS(strtoll)

# autoheader will use this to create config.h
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([Makefile])

AC_SUBST(GLOBUS_CC)
AC_SUBST(GLOBUS_CFLAGS)
AC_SUBST(GLOBUS_CPPFLAGS)
AC_SUBST(GLOBUS_PKG_LIBS)
AC_SUBST(GLOBUS_LDFLAGS)

AC_OUTPUT
