/* -*- fundamental -*- */
/* $NetBSD: conf_lex.l,v 1.2 2001/02/07 01:14:57 bjh21 Exp $ */
/*-
 * Copyright (c) 1998 Ben Harris
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * This is part of aund, an implementation of Acorn Universal
 * Networking for Unix.
 */	
/*
 * conf_lex.l - lexer for configuration files.
 */

%option noyywrap
%option prefix="cfy"
%option outfile="lex.yy.c"
%{
#include <sys/types.h>
#include <sys/stat.h>

#include <err.h>

#include "extern.h"
#include "fileserver.h"

#ifndef S_IFIFO
#define S_IFIFO 0
#endif
#ifndef S_IFCHR
#define S_IFCHR 0
#endif
#ifndef S_IFDIR
#define S_IFDIR 0
#endif
#ifndef S_IFDIR
#define S_IFDIR 0
#endif
#ifndef S_IFBLK
#define S_IFBLK 0
#endif
#ifndef S_IFREG
#define S_IFREG 0
#endif
#ifndef S_IFLNK
#define S_IFLNK 0
#endif
#ifndef S_IFSOCK
#define S_IFSOCK 0
#endif
#ifndef S_IFWHT
#define S_IFWHT 0
#endif

#define CF_EOF 0
#define CF_ODDWORD 2
#define CF_BOOLEAN 3
#define CF_WORD 4

static void conf_read_file __P((const char *));
static void conf_cmd_debug __P((void));
static void conf_cmd_urd __P((void));
static void conf_cmd_opt4 __P((void));
static void conf_cmd_typemap_name __P((void));
static void conf_cmd_typemap_perm __P((void));
static void conf_cmd_typemap_type __P((mode_t));
static void conf_cmd_typemap_default __P((void));

static int cfylex __P((int start, void *thing));

#define YY_DECL static int cfylex(start, thing) int start; void *thing;

%}


%s BORING
%s TYPEMAP
%s TYPEMAP_NAME
%s TYPEMAP_PERM
%s TYPEMAP_TYPE
%s BOOLEAN
%%
	int cfy_line;

	BEGIN(start);

#[^\n]*\n		cfy_line++; BEGIN(INITIAL);
[[:blank:]]+		/* Ignore whitespace except insofar as it splits words */
\\\n			cfy_line++;
\n			cfy_line++; BEGIN(INITIAL);

<INITIAL>{
  typemap	BEGIN(TYPEMAP);
  debug		BEGIN(BORING); conf_cmd_debug();
  urd		BEGIN(BORING); conf_cmd_urd();
  opt4		BEGIN(BORING); conf_cmd_opt4();
}
<TYPEMAP>{
  name		BEGIN(BORING); conf_cmd_typemap_name(); 
  perm		BEGIN(BORING); conf_cmd_typemap_perm();
  type		BEGIN(TYPEMAP_TYPE);
  default	BEGIN(BORING); conf_cmd_typemap_default();
}
<TYPEMAP_TYPE>{
  fifo		BEGIN(BORING); conf_cmd_typemap_type(S_IFIFO);
  chr		BEGIN(BORING); conf_cmd_typemap_type(S_IFCHR);
  dir		BEGIN(BORING); conf_cmd_typemap_type(S_IFDIR);
  blk		BEGIN(BORING); conf_cmd_typemap_type(S_IFBLK);
  reg		BEGIN(BORING); conf_cmd_typemap_type(S_IFREG);
  lnk		BEGIN(BORING); conf_cmd_typemap_type(S_IFLNK);
  sock		BEGIN(BORING); conf_cmd_typemap_type(S_IFSOCK);
  wht		BEGIN(BORING); conf_cmd_typemap_type(S_IFWHT);
}
<BOOLEAN>{
  yes|on|true	*(int *)thing = 1; return CF_BOOLEAN;
  no|off|false	*(int *)thing = 0; return CF_BOOLEAN;
}

[^[:blank:]\n]+		return (YY_START == BORING) ? CF_WORD : CF_ODDWORD;
<<EOF>>			return CF_EOF;

%%

void
conf_init()
{
	conf_read_file("/etc/aund.conf");
}

static void
conf_read_file(path)
	const char *path;
{
        FILE *f;

	f = fopen(path, "r");
	if (f == NULL) err(1, "%s", path);
	cfyin = f;
	switch (cfylex(INITIAL, NULL)) {
	case CF_ODDWORD:
		errx(1, "Syntax error");
		fclose(f);
	}
}

static void
conf_cmd_debug()
{
	if (cfylex(BOOLEAN, &debug) != CF_BOOLEAN)
		errx(1, "no boolean for debug");
}

static void
conf_cmd_urd()
{

	if (cfylex(BORING, NULL) != CF_WORD)
		errx(1, "no urd specified");
	urd = malloc(cfyleng + 1);
	strcpy(urd, cfytext);
}

static void
conf_cmd_opt4()
{
	char *endptr;

	if (cfylex(BORING, NULL) != CF_WORD)
		errx(1, "no boot option specified");
	opt4 = strtol(cfytext, &endptr, 0);
	if (*endptr != '\0')
		errx(1, "bad boot option");
}

static void
conf_cmd_typemap_name()
{
	char *re;
	int type;
	
	if (cfylex(BORING, NULL) != CF_WORD)
		errx(1, "no re for typemap");
	re = malloc(cfyleng + 1);
	strcpy(re, cfytext);
	if (cfylex(BORING, NULL) != CF_WORD)
		errx(1, "no type for typemap");
	if (sscanf(cfytext, "%x", &type) != 1)
		errx(1, "bad type for typemap");
	if (fs_add_typemap_name(re, type) == -1)
		errx(1, "problem adding typemap");
}

static void
conf_cmd_typemap_perm()
{
	mode_t perm;
	int type;

	if (cfylex(BORING, NULL) != CF_WORD)
		errx(1, "no perm for typemap");
	if (sscanf(cfytext, "%o", &perm) != 1)
		errx(1, "bad perm for typemap");
	if (cfylex(BORING, NULL) != CF_WORD)
		errx(1, "no type for typemap");
	if (sscanf(cfytext, "%x", &type) != 1)
		errx(1, "bad type for typemap");
	if (fs_add_typemap_mode(perm, perm, type) == -1)
		errx(1, "problem adding typemap");
}

static void
conf_cmd_typemap_type(utype)
	mode_t utype;
{
	int type;

	if (cfylex(BORING, NULL) != CF_WORD)
		errx(1, "no type for typemap");
	if (sscanf(cfytext, "%x", &type) != 1)
		errx(1, "bad type for typemap");
	if (fs_add_typemap_mode(utype, S_IFMT, type) == -1)
		errx(1, "problem adding typemap");
}

static void
conf_cmd_typemap_default()
{
	int type;

	if (cfylex(BORING, NULL) != CF_WORD)
		errx(1, "no type for typemap");
	if (sscanf(cfytext, "%x", &type) != 1)
		errx(1, "bad type for typemap");
	if (fs_add_typemap_default(type) == -1)
		errx(1, "problem adding typemap");
}
