dnl aclocal.m4 generated automatically by aclocal 1.4

dnl Copyright (C) 1994, 1995-8, 1999 Free Software Foundation, Inc.
dnl This file is free software; the Free Software Foundation
dnl gives unlimited permission to copy and/or distribute it,
dnl with or without modifications, as long as this notice is preserved.

dnl This program is distributed in the hope that it will be useful,
dnl but WITHOUT ANY WARRANTY, to the extent permitted by law; without
dnl even the implied warranty of MERCHANTABILITY or FITNESS FOR A
dnl PARTICULAR PURPOSE.

# Copyright (C) 1998-2000 Joe Orton.  
# This file is free software; you may copy and/or distribute it with
# or without modifications, as long as this notice is preserved.
# This software is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY, to the extent permitted by law; without even
# the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
# PURPOSE.

# The above license applies to THIS FILE ONLY, the neon library code
# itself may be copied and distributed under the terms of the GNU
# LGPL, see COPYING.LIB for more details

# This file is part of the neon HTTP/WebDAV client library.
# See http://www.webdav.org/neon/ for the latest version. 
# Please send any feedback to <neon@webdav.org>

#
# Usage:
#
#      NEON_LIBRARY
# or   NEON_BUNDLED(srcdir, [ACTIONS-IF-BUNDLED]) 
# or   NEON_VPATH_BUNDLED(srcdir, builddir, [ACTIONS-IF-BUNDLED])
#
#   where srcdir is the location of bundled neon 'src' directory.
#   If using a VPATH-enabled build, builddir is the location of the
#   build directory corresponding to srcdir.
#
#   If a bundled build *is* being used, ACTIONS-IF-BUNDLED will be
#   evaluated. These actions should ensure that 'make' is run
#   in srcdir, and that one of NEON_NORMAL_BUILD or NEON_LIBTOOL_BUILD 
#   is called.
#
# After calling one of the above macros, if the NEON_NEED_XML_PARSER
# variable is set to "yes", then you must configure an XML parser
# too. You can do this your own way, or do it easily using the
# NEON_XML_PARSER() macro. Example usage for where we have bundled the
# neon sources in a directory called libneon, and bundled expat
# sources in a directory called 'expat'.
#
#   NEON_BUNDLED(libneon, [
#	SUBDIRS="$SUBDIRS libneon"
#	NEON_XML_PARSER(expat)
#	NEON_NORMAL_BUILD
#   ])
#
# Alternatively, for a simple standalone app with neon as a
# dependancy, use just:
#
#   NEON_LIBRARY
# 
# and rely on the user installing neon correctly.
#
# You are free to configure an XML parser any other way you like,
# but the end result must be, either expat or libxml will get linked
# in, and HAVE_EXPAT or HAVE_LIBXML is defined appropriately.
#
# To set up the bundled build environment, call 
#
#    NEON_NORMAL_BUILD
# or
#    NEON_LIBTOOL_BUILD
# 
# depending on whether you are using libtool to build, or not.
# Both these macros take an optional argument specifying the set
# of object files you wish to build: if the argument is not given,
# all of neon will be built.

AC_DEFUN([NEON_BUNDLED],[

neon_bundled_srcdir=$1
neon_bundled_builddir=$1

NEON_COMMON_BUNDLED([$2])

])

AC_DEFUN([NEON_VPATH_BUNDLED],[

neon_bundled_srcdir=$1
neon_bundled_builddir=$2
NEON_COMMON_BUNDLED([$3])

])

AC_DEFUN([NEON_COMMON_BUNDLED],[

AC_ARG_WITH(included-neon,
[  --with-included-neon    Force use of included neon library ],
[neon_force_included="$withval"],
[neon_force_included="no"])

NEON_COMMON

if test "$neon_force_included" = "yes"; then
	# The colon is here so there is something to evaluate
	# here in case the argument was not passed.
	:
	$1
fi

])

dnl Not got any bundled sources:
AC_DEFUN([NEON_LIBRARY],[

neon_force_included=no
neon_bundled_srcdir=
neon_bundled_builddir=

NEON_COMMON

])

AC_DEFUN([NEON_VERSIONS], [

# Define the current versions.
NEON_VERSION_MAJOR=0
NEON_VERSION_MINOR=12
NEON_VERSION_RELEASE=0
NEON_VERSION_TAG=

NEON_VERSION="${NEON_VERSION_MAJOR}.${NEON_VERSION_MINOR}.${NEON_VERSION_RELEASE}${NEON_VERSION_TAG}"

# the libtool interface version is
#   current:revision:age
NEON_INTERFACE_VERSION="12:0:0"

AC_DEFINE_UNQUOTED(NEON_VERSION, "${NEON_VERSION}", 
	[Define to be the neon version string])
AC_DEFINE_UNQUOTED(NEON_VERSION_MAJOR, [(${NEON_VERSION_MAJOR})],
	[Define to be major number of neon version])
AC_DEFINE_UNQUOTED(NEON_VERSION_MINOR, [(${NEON_VERSION_MINOR})],
	[Define to be minor number of neon version])

])

dnl Define the minimum required version
AC_DEFUN([NEON_REQUIRE], [
neon_require_major=$1
neon_require_minor=$2
])

dnl Check that the external library found in a given location
dnl matches the min. required version (if any)
dnl Usage:
dnl    NEON_CHECK_VERSION(instroot[, ACTIONS-IF-OKAY[, ACTIONS-IF-FAILURE]])
dnl
AC_DEFUN([NEON_CHECK_VERSION], [

if test "x$neon_require_major" = "x"; then
    :
    $2
else
    config=$1/bin/neon-config
    oLIBS="$LIBS"
    oCFLAGS="$CFLAGS"
    CFLAGS="$CFLAGS `$config --cflags`"
    LIBS="$LIBS `$config --libs`"
    ver=`$config --version`
    AC_MSG_CHECKING(for neon library version)
    AC_TRY_RUN([
#include <http_utils.h>
    
int main(void) {
return neon_version_minimum($neon_require_major, $neon_require_minor);
}
], [
    AC_MSG_RESULT([okay (found $ver)])
    LIBS="$oLIBS"
    CFLAGS="$oCFLAGS"
    $2
], [
    AC_MSG_RESULT([failed, found $ver wanted >=$neon_require_major.$neon_require_minor])
    LIBS="$oLIBS"
    CFLAGS="$oCFLAGS"
    $3
])

fi

])

AC_DEFUN([NEON_COMMON],[

NEON_VERSIONS

AC_ARG_WITH(neon,
	[  --with-neon	          Specify location of neon library ],
	[neon_loc="$withval"])

AC_MSG_CHECKING(for neon location)

if test "$neon_force_included" = "no"; then
    # We don't have an included neon source directory,
    # or they aren't force us to use it.

    if test -z "$neon_loc"; then
	# Look in standard places
	for d in /usr /usr/local; do
	    if test -x $d/bin/neon-config; then
		neon_loc=$d
	    fi
	done
    fi

    if test -x $neon_loc/bin/neon-config; then
	# Found it!
	AC_MSG_RESULT(found in $neon_loc)
	NEON_CHECK_VERSION([$neon_loc], [
	  NEON_CONFIG=$neon_loc/bin/neon-config
	  CFLAGS="$CFLAGS `$NEON_CONFIG --cflags`"
	  NEONLIBS="$NEONLIBS `$NEON_CONFIG --libs`"
	  neon_library_message="library in $neon_loc (`$NEON_CONFIG --version`)"
	  neon_xml_parser_message="using whatever libneon uses"
	  neon_got_library=yes
        ], [
	  neon_got_library=no
	])
    else
	neon_got_library=no
    fi

    if test "$neon_got_library" = "no"; then 
	if test -n "$neon_bundled_srcdir"; then
	    # Couldn't find external neon, forced to use bundled sources
	    neon_force_included="yes"
	else
	    # Couldn't find neon, and don't have bundled sources
	    AC_MSG_ERROR(could not find neon)
	fi
    fi
fi

# This isn't a simple 'else' branch, since neon_force_included
# is set to yes if the search fails.

if test "$neon_force_included" = "yes"; then
    AC_MSG_RESULT([using supplied ($NEON_VERSION)])
    CFLAGS="$CFLAGS -I$neon_bundled_srcdir"
    LDFLAGS="$LDFLAGS -L$neon_bundled_builddir"
    NEONLIBS="$LIBS -lneon"
    NEON_BUILD_BUNDLED="yes"
    LIBNEON_SOURCE_CHECKS
    NEON_NEED_XML_PARSER=yes
    neon_library_message="included libneon (${NEON_VERSION})"
else
    # Don't need to configure an XML parser
    NEON_NEED_XML_PARSER=no
    NEON_BUILD_BUNDLED="yes"
fi

AC_SUBST(NEON_BUILD_BUNDLED)
AC_SUBST(NEONLIBS)

])

dnl Call these checks when compiling the libneon source package.

AC_DEFUN([LIBNEON_SOURCE_CHECKS],[

AC_C_BIGENDIAN
AC_C_INLINE
AC_C_CONST

AC_CHECK_HEADERS(stdarg.h string.h strings.h sys/time.h regex.h \
	stdlib.h unistd.h limits.h sys/select.h arpa/inet.h)

AC_REPLACE_FUNCS(strcasecmp)

AC_SEARCH_LIBS(gethostbyname, nsl)

AC_SEARCH_LIBS(socket, socket inet)

NEON_SSL()

])

dnl Call to put lib/snprintf.o in LIBOBJS and define HAVE_SNPRINTF_H
dnl if snprintf isn't in libc.

AC_DEFUN([NEON_REPLACE_SNPRINTF], [

dnl Check for snprintf
AC_CHECK_FUNC(snprintf,,
	AC_DEFINE(HAVE_SNPRINTF_H, 1, [Define if need to include snprintf.h])
	LIBOBJS="$LIBOBJS lib/snprintf.o" )

])

dnl Common macro to NEON_LIBTOOL_BUILD and NEON_NORMAL_BUILD
dnl Sets NEONOBJS appropriately if it has not already been set.
dnl 
dnl NOT FOR EXTERNAL USE: use LIBTOOL_BUILD or NORMAL_BUILD.
dnl

dnl turn off webdav, boo hoo.
AC_DEFUN([NEON_WITHOUT_WEBDAV], [
neon_no_webdav=yes
NEON_NEED_XML_PARSER=no
neon_xml_parser_message="none needed"
])

AC_DEFUN([NEON_COMMON_BUILD], [

dnl Cunning hack: $1 is passed as the number of arguments passed
dnl to the NORMAL or LIBTOOL macro, so we know whether they 
dnl passed any arguments or not.

ifelse($1, 0, [

 # Using the default set of object files to build.

 # 'o' is the object extension in use
 o=$NEON_OBJEXT

 AC_MSG_CHECKING(whether to enable WebDAV support in neon)

 dnl Did they want DAV support?
 if test "x$neon_no_webdav" = "xyes"; then
  # No WebDAV support
  NEONOBJS="http_request.$o http_basic.$o string_utils.$o uri.$o  \
    dates.$o ne_alloc.$o base64.$o md5.$o http_utils.$o		  \
    socket.$o http_auth.$o http_cookies.$o http_redirect.$o"

  AC_MSG_RESULT(no)

 else
	
 # WebDAV support
  NEONOBJS="http_request.$o http_basic.$o dav_basic.$o	\
    dav_207.$o string_utils.$o dates.$o ne_alloc.$o	\
    hip_xml.$o base64.$o md5.$o http_utils.$o		\
    uri.$o socket.$o http_auth.$o dav_props.$o		\
    http_cookies.$o dav_locks.$o http_redirect.$o"

  # Turn on DAV locking please then.
  AC_DEFINE(USE_DAV_LOCKS, 1, [Support WebDAV locking through the library])

  AC_MSG_RESULT(yes)

 fi

], [
 
 # Using a specified set of object files.
 NEONOBJS=$1

])

AC_SUBST(NEON_TARGET)
AC_SUBST(NEON_OBJEXT)
AC_SUBST(NEONOBJS)
AC_SUBST(NEON_LINK_FLAGS)

AC_PATH_PROG(AR, ar)

])

# The libtoolized build case:
AC_DEFUN([NEON_LIBTOOL_BUILD], [

NEON_TARGET=libneon.la
NEON_OBJEXT=lo

NEON_COMMON_BUILD($#, $*)

])

# The non-libtool build case:
AC_DEFUN([NEON_NORMAL_BUILD], [

NEON_TARGET=libneon.a
NEON_OBJEXT=o

NEON_COMMON_BUILD($#, $*)

])

# Copyright (C) 1998-2000 Joe Orton.  
# This file is free software; you may copy and/or distribute it with
# or without modifications, as long as this notice is preserved.
# This software is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY, to the extent permitted by law; without even
# the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
# PURPOSE.

# The above license applies to THIS FILE ONLY, the neon library code
# itself may be copied and distributed under the terms of the GNU
# LGPL, see COPYING.LIB for more details

# This file is part of the neon HTTP/WebDAV client library.
# See http://www.webdav.org/neon/ for the latest version. 
# Please send any feedback to <neon@webdav.org>
# $Id: aclocal.m4,v 1.2 2001/03/14 17:22:24 mszeredi Exp $

# Check for XML parser.
# Supports:
#  *  libxml (requires version 1.8.3 or later)
#  *  expat in -lexpat
#  *  expat in -lxmlparse and -lxmltok (as packaged by Debian/Red Hat)
#  *  Bundled expat if a directory name argument is passed
#     -> expat dir must contain minimal expat sources, i.e.
#        xmltok, xmlparse sub-directories.  See sitecopy/cadaver for
#	 examples of how to do this.
#
# Usage: 
#  NEON_XML_PARSER()
# or
#  NEON_XML_PARSER(expat-dir)

AC_DEFUN([NEON_XML_PARSER], [

if test "$NEON_NEED_XML_PARSER" = "yes"; then

AC_ARG_ENABLE([libxml],
	[  --enable-libxml         force use of libxml ],
	[neon_force_libxml=$enableval],
	[neon_force_libxml=no])

AC_ARG_WITH([expat],
[  --with-expat=DIR        specify Expat location], [
	if test "$withval" != "no"; then
		case "$withval" in
		*/libexpat.la)
			neon_using_libtool_expat=yes
			withval=`echo $withval | sed 's:/libexpat.la$::'`
		esac
		if test -r "$withval/xmlparse.h"; then
			AC_DEFINE(HAVE_EXPAT, 1, [Define if you have expat])
			CFLAGS="$CFLAGS -I$withval"
			dnl add the library (if it isn't a libtool library)
			if test -z "$neon_using_libtool_expat"; then
				NEONLIBS="$NEONLIBS -L$withval -lexpat"
			fi

			neon_xml_parser_message="expat in $withval"
			neon_found_parser="yes"
		fi
	fi
],[
	neon_found_parser="no"
])

if test "$neon_found_parser" = "no" -a "$neon_force_libxml" = "no"; then
	AC_CHECK_LIB(expat, XML_Parse,
		neon_expat_libs="-lexpat" neon_found_parser="expat",
		AC_CHECK_LIB(xmlparse, XML_Parse,
			neon_expat_libs="-lxmltok -lxmlparse" 
			neon_found_parser="expat",
			neon_found_parser="no",
			-lxmltok )
		)
fi

if test "$neon_found_parser" = "no"; then
	# Have we got libxml 1.8.3 or later?
	AC_CHECK_PROG(XML_CONFIG, xml-config, xml-config)
	if test "$XML_CONFIG" != ""; then
		# Check for recent library
		oLIBS="$LIBS"
		oCFLAGS="$CFLAGS"
		NEWLIBS="`$XML_CONFIG --libs`"
		LIBS="$LIBS $NEWLIBS"
		CFLAGS="$CFLAGS `$XML_CONFIG --cflags`"
		AC_CHECK_LIB(xml, xmlCreatePushParserCtxt,
			neon_found_parser="libxml" neon_xml_parser_message="libxml"
			NEONLIBS="$NEONLIBS $NEWLIBS"
			AC_DEFINE(HAVE_LIBXML, 1, [Define if you have libxml])
			,
			CFLAGS="$oCFLAGS"
			LIBS="$oLIBS"
			AC_WARN([cannot use old libxml (1.8.3 or newer required)])
		)
	fi
fi

if test "$neon_found_parser" = "expat"; then
	# This is crap. Maybe just use AC_CHECK_HEADERS and use the
	# right file by ifdef'ing is best
	AC_CHECK_HEADER(xmlparse.h,
	[neon_expat_incs="" neon_found_expatincs="yes"],
	AC_CHECK_HEADER(xmltok/xmlparse.h,
	[neon_expat_incs="-I/usr/include/xmltok" neon_found_expatincs="yes"],
	))
	if test "$neon_found_expatincs" = "yes"; then
		AC_DEFINE(HAVE_EXPAT, 1, [Define if you have expat])
		if test "$neon_expat_incs"; then
			CFLAGS="$CFLAGS $neon_expat_incs"
		fi	
		NEONLIBS="$NEONLIBS $neon_expat_libs"
	else
	       AC_MSG_ERROR(["found expat library but could not find xmlparse.h"])
	fi
	neon_xml_parser_message="expat in $neon_expat_libs"
fi

if test "$neon_found_parser" = "no" ; then

    if test "x$1" != "x"; then
	# Use the bundled expat sources
	LIBOBJS="$LIBOBJS $1/xmltok/xmltok.o $1/xmltok/xmlrole.o $1/xmlparse/xmlparse.o $1/xmlparse/hashtable.o"
	CFLAGS="$CFLAGS -DXML_DTD -I$1/xmlparse -I$1/xmltok"
	AC_MSG_RESULT(using supplied expat XML parser)	
	AC_DEFINE(HAVE_EXPAT, 1, [Define if you have expat] )
	neon_xml_parser_message="supplied expat in $1"
    else
	AC_MSG_ERROR([no XML parser was found])
    fi

fi

fi

])

# Copyright (C) 1998-2000 Joe Orton.  
# This file is free software; you may copy and/or distribute it with
# or without modifications, as long as this notice is preserved.
# This software is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY, to the extent permitted by law; without even
# the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
# PURPOSE.

# The above license applies to THIS FILE ONLY, the neon library code
# itself may be copied and distributed under the terms of the GNU
# LGPL, see COPYING.LIB for more details

# This file is part of the neon HTTP/WebDAV client library.
# See http://www.webdav.org/neon/ for the latest version. 
# Please send any feedback to <neon@webdav.org>
# $Id: aclocal.m4,v 1.2 2001/03/14 17:22:24 mszeredi Exp $

# $1 specifies the location of the bundled neon "src" directory, or
# is empty if none is bundled. $1 specifies the location of the bundled
# expat directory, or is empty if none is bundled.

AC_DEFUN([NEON_SSL],[

AC_ARG_WITH(ssl, 
	[  --with-ssl[=DIR]        enable OpenSSL support ],,
	[with_ssl="no"])

# In an ideal world, we would default to with_ssl="yes".
# But this might lead to packagers of neon-enabled apps
# unknowingly exporting crypto binaries.

AC_MSG_CHECKING(for OpenSSL)

if test "$with_ssl" = "yes"; then
	# They didn't specify a location: look in
	# some usual places
	neon_ssl_dirs="/usr/local/ssl /usr/ssl /usr"
	neon_ssl_location=""

	for d in $neon_ssl_dirs; do
		if test -r $d/include/openssl/ssl.h; then
			neon_ssl_location=$d
			break
		fi
	done
elif test "$with_ssl" = "no"; then
	neon_ssl_location=""
else
	neon_ssl_location=$with_ssl
fi

if test -n "$neon_ssl_location"; then
	CFLAGS="$CFLAGS -I${neon_ssl_location}/include"
	LDFLAGS="$LDFLAGS -L${neon_ssl_location}/lib"
	NEONLIBS="$NEONLIBS -lssl -lcrypto"
	AC_DEFINE([ENABLE_SSL], 1, [Define to enable OpenSSL support])
	AC_MSG_RESULT(found in $neon_ssl_location)
else
	AC_MSG_RESULT(not found)
fi

])


