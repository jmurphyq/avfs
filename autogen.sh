#! /bin/sh

aclocal -I macros
autoheader
libtoolize -c -f
automake -a -c
autoconf
