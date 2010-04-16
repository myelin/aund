/* -*- fundamental -*- */
/*-
 * Copyright (c) 2010 Simon Tatham
 * Copyright (c) 1998, 2010 Ben Harris
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
#include <string.h>

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

static void conf_read_file(const char *);
static void conf_cmd_debug(void);
static void conf_cmd_syslog(void);
static void conf_cmd_root(void);
static void conf_cmd_urd(void);
static void conf_cmd_pwfile(void);
static void conf_cmd_lib(void);
static void conf_cmd_beebem(void);
static void conf_cmd_infofmt(void);
static void conf_cmd_opt4(void);
static void conf_cmd_typemap_name(void);
static void conf_cmd_typemap_perm(void);
static void conf_cmd_typemap_type(mode_t);
static void conf_cmd_typemap_default(void);

static int cfylex(int start, void *thing);

#define YY_DECL static int cfylex(int start, void *thing)

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
  debug		BEGIN(BORING); conf_cmd_debug(); BEGIN(INITIAL);
  syslog	BEGIN(BORING); conf_cmd_syslog(); BEGIN(INITIAL);
  root		BEGIN(BORING); conf_cmd_root(); BEGIN(INITIAL);
  lib|library	BEGIN(BORING); conf_cmd_lib(); BEGIN(INITIAL);
  urd		BEGIN(BORING); conf_cmd_urd(); BEGIN(INITIAL);
  pwfile	BEGIN(BORING); conf_cmd_pwfile(); BEGIN(INITIAL);
  opt4		BEGIN(BORING); conf_cmd_opt4(); BEGIN(INITIAL);
  beebem	BEGIN(BORING); conf_cmd_beebem(); BEGIN(INITIAL);
  info([_-]?(fmt|format))	BEGIN(BORING); conf_cmd_infofmt(); BEGIN(INITIAL);
}
<TYPEMAP>{
  name		BEGIN(BORING); conf_cmd_typemap_name(); BEGIN(INITIAL);
  perm		BEGIN(BORING); conf_cmd_typemap_perm(); BEGIN(INITIAL);
  type		BEGIN(TYPEMAP_TYPE);
  default	BEGIN(BORING); conf_cmd_typemap_default(); BEGIN(INITIAL);
}
<TYPEMAP_TYPE>{
  fifo		BEGIN(BORING); conf_cmd_typemap_type(S_IFIFO); BEGIN(INITIAL);
  chr		BEGIN(BORING); conf_cmd_typemap_type(S_IFCHR); BEGIN(INITIAL);
  dir		BEGIN(BORING); conf_cmd_typemap_type(S_IFDIR); BEGIN(INITIAL);
  blk		BEGIN(BORING); conf_cmd_typemap_type(S_IFBLK); BEGIN(INITIAL);
  reg		BEGIN(BORING); conf_cmd_typemap_type(S_IFREG); BEGIN(INITIAL);
  lnk		BEGIN(BORING); conf_cmd_typemap_type(S_IFLNK); BEGIN(INITIAL);
  sock		BEGIN(BORING); conf_cmd_typemap_type(S_IFSOCK); BEGIN(INITIAL);
  wht		BEGIN(BORING); conf_cmd_typemap_type(S_IFWHT); BEGIN(INITIAL);
}
<BOOLEAN>{
  yes|on|true	*(int *)thing = 1; return CF_BOOLEAN;
  no|off|false	*(int *)thing = 0; return CF_BOOLEAN;
}

[^[:blank:]\n]+		return (YY_START == BORING) ? CF_WORD : CF_ODDWORD;
<<EOF>>			return CF_EOF;

%%

void
conf_init(const char *path)
{

	conf_read_file(path);
}

static void
conf_read_file(const char *path)
{
        FILE *f;

	f = fopen(path, "r");
	if (f == NULL) err(1, "%s", path);
	cfyin = f;
	switch (cfylex(INITIAL, NULL)) {
	case CF_ODDWORD:
		errx(1, "Syntax error: unrecognised keyword '%s'", cfytext);
		fclose(f);
	}
}

static void
conf_cmd_debug(void)
{

	if (cfylex(BOOLEAN, &debug) != CF_BOOLEAN)
		errx(1, "no boolean for debug");
}

static void
conf_cmd_syslog(void)
{

	if (cfylex(BOOLEAN, &using_syslog) != CF_BOOLEAN)
		errx(1, "no boolean for syslog");
}

static void
conf_cmd_root(void)
{

	if (cfylex(BORING, NULL) != CF_WORD)
		errx(1, "no root specified");
	root = malloc(cfyleng + 1);
	strcpy(root, cfytext);
}

static void
conf_cmd_urd(void)
{

	if (cfylex(BORING, NULL) != CF_WORD)
		errx(1, "no urd specified");
	fixedurd = malloc(cfyleng + 1);
	strcpy(fixedurd, cfytext);
}

static void
conf_cmd_pwfile(void)
{

	if (cfylex(BORING, NULL) != CF_WORD)
		errx(1, "no urd specified");
	pwfile = malloc(cfyleng + 1);
	strcpy(pwfile, cfytext);
}

static void
conf_cmd_lib(void)
{

	if (cfylex(BORING, NULL) != CF_WORD)
		errx(1, "no library specified");
	lib = malloc(cfyleng + 1);
	strcpy(lib, cfytext);
}

static void
conf_cmd_beebem(void)
{
	int ret = cfylex(BORING, NULL);

	if (ret == CF_WORD && !strcasecmp(cfytext, "ingress")) {
		beebem_ingress = 1;
		ret = cfylex(BORING, NULL);
	}

	if (ret != CF_WORD)
		errx(1, "no beebem configuration file specified");

	beebem_cfg_file = malloc(cfyleng + 1);
	strcpy(beebem_cfg_file, cfytext);
}

static void
conf_cmd_infofmt(void)
{

	if (cfylex(BORING, NULL) != CF_WORD)
		errx(1, "no *INFO format keyword specified");
	if (!strcasecmp(cfytext, "riscos"))
		infoformat = FS_INFO_RISCOS;
	else if (!strcasecmp(cfytext, "sj"))
		infoformat = FS_INFO_SJ;
}

static void
conf_cmd_opt4(void)
{
	char *endptr;

	if (cfylex(BORING, NULL) != CF_WORD)
		errx(1, "no boot option specified");
	default_opt4 = strtol(cfytext, &endptr, 0);
	if (*endptr != '\0')
		errx(1, "bad boot option");
}

static void
conf_cmd_typemap_name(void)
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
conf_cmd_typemap_perm(void)
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
conf_cmd_typemap_type(mode_t utype)
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
conf_cmd_typemap_default(void)
{
	int type;

	if (cfylex(BORING, NULL) != CF_WORD)
		errx(1, "no type for typemap");
	if (sscanf(cfytext, "%x", &type) != 1)
		errx(1, "bad type for typemap");
	if (fs_add_typemap_default(type) == -1)
		errx(1, "problem adding typemap");
}
