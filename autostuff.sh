#!/bin/sh 

if test "x$1" = "xclean"; then
  rm -f Makefile.in config.h.in configure depcomp install-sh missing ylwrap aclocal.m4
else
  aclocal && autoconf && autoheader && automake -a
fi
rm -rf autom4te.cache
