# $NetBSD: Makefile,v 1.1 2001/02/06 23:54:45 bjh21 Exp $

PROG=aund
NOMAN=noman
WARNS=0
SRCS = aund.c conf_lex.l fileserver.c fs_cli.c fs_examine.c fs_fileio.c \
	fs_misc.c fs_handle.c fs_util.c fs_error.c fs_nametrans.c \
	fs_filetype.c

.include <bsd.prog.mk>
