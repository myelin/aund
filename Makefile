# $NetBSD: Makefile,v 1.2 2001/02/08 15:55:53 bjh21 Exp $

PROG=aund
NOMAN=noman
WARNS=0
SRCS = aund.c conf_lex.l fileserver.c fs_cli.c fs_examine.c fs_fileio.c \
	fs_misc.c fs_handle.c fs_util.c fs_error.c fs_nametrans.c \
	fs_filetype.c

# I know I use GCC extensions, and I'm suitably ashamed.
LINTFLAGS+=	-X 20,39

.include <bsd.prog.mk>
