##########################################################################
# University of Illinois/NCSA Open Source License
#
# Copyright © 2003-2008, NCSA.  All rights reserved.
#
# Developed by:
#
# Storage Enabling Technologies (SET)
#
# Nation Center for Supercomputing Applications (NCSA)
#
# http://dims.ncsa.uiuc.edu/set/uberftp
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the .Software.),
# to deal with the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
#    + Redistributions of source code must retain the above copyright notice,
#      this list of conditions and the following disclaimers.
#
#    + Redistributions in binary form must reproduce the above copyright
#      notice, this list of conditions and the following disclaimers in the
#      documentation and/or other materials provided with the distribution.
#
#    + Neither the names of SET, NCSA
#      nor the names of its contributors may be used to endorse or promote
#      products derived from this Software without specific prior written
#      permission.
#
# THE SOFTWARE IS PROVIDED .AS IS., WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE CONTRIBUTORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
# OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
# ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS WITH THE SOFTWARE.
############################################################################

# SUBDIRS is an automake macro which contains 
# the list of subdirectories that have Makefile.am's
SUBDIRS=

# Sources contains all of the source files in the directory.
# Header files are included because automake will generate dependency targets 
# for them and include them in a source distribution.
#
# If this directory builds multiple programs or libraries than these sources
# will need to be seperated into seperate macros.
Sources= \
	cmds.c     filetree.c   ftp_s.h       logical.c  network.c  radix.h    \
	cmds.h     filetree.h   gsi.c         logical.h  network.h  settings.c \
	config.h   ftp.c        gsi.h         main.c     output.c   settings.h \
	errcode.c  ftp.h        misc.c        output.h   unix.c     unix.h     \
	errcode.h  ftp_s.c      linterface.h  misc.h     radix.c    nc.h       \
	nc.c       ftp_a.c      ftp_a.h       ftp_eb.c   ftp_eb.h   ml.c       \
	ml.h       cksum.c      cksum.h       perf.c     perf.h

uberftp_SOURCES=$(Sources)
uberftp_LDFLAGS=@LDFLAGS@ @GLOBUS_LDFLAGS@
uberftp_LDADD=@LIBS@ @GLOBUS_LIBS@
uberftp_CFLAGS=@CFLAGS@ @GLOBUS_CFLAGS@ -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGE_FILES
bin_PROGRAMS=uberftp
man_MANS=uberftp.1

#cmds2man_SOURCES=$(Sources)
#cmds2man_LDFLAGS=@LDFLAGS@ @GLOBUS_LDFLAGS@
#cmds2man_LDADD=@LIBS@ @GLOBUS_LIBS@
#cmds2man_CFLAGS=@CFLAGS@ @GLOBUS_CFLAGS@ -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGE_FILES -DCMDS2MAN

srcpkg: distdir
	-cp pkg_data_src.gpt $(distdir)
	-cp Changelog $(distdir)
	-cp Changelog.mssftp $(distdir)
	-cp copyright $(distdir)
	-cp filelist $(distdir)
	-cp uberftp.1 $(distdir)
	-cp mssftp.1 $(distdir)
	-chmod -R a+r $(distdir)
	tar chozf $(distdir).tar.gz $(distdir)
	-rm -rf $(distdir)

gpt-install: install
