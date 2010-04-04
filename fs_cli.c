/* $NetBSD: fs_cli.c,v 1.3 2009/01/02 23:33:04 bjh21 Exp $ */
/* Berkeley copyright because of fs_cmd_info(), which is basically ls(1) */
/*
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*
 * fs_cli.c - command-line interpreter for file server
 */

#include <sys/types.h>

#include <assert.h>
#include <ctype.h>
#include <grp.h>
#include <libgen.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <utmp.h>

#include "aun.h"
#include "fs_proto.h"
#include "fs_errors.h"
#include "extern.h"
#include "fileserver.h"

#include "bsd_libc.h"

typedef void fs_cmd_impl __P((struct fs_context *, char *));

struct fs_cmd {
	char	*full;
	char	*min;
	fs_cmd_impl	*impl;
};

static fs_cmd_impl fs_cmd_i_am;
static fs_cmd_impl fs_cmd_info;
static fs_cmd_impl fs_cmd_lib;
static fs_cmd_impl fs_cmd_sdisc;
static fs_cmd_impl fs_cmd_pass;

static int fs_cli_match __P((char *word, const struct fs_cmd *cmd));
static void fs_cli_unrec __P((struct fs_context *, char *));

static char *printtime __P((time_t));

static const struct fs_cmd cmd_tab[] = {
	{"I", 		"I",	fs_cmd_i_am,	}, /* Odd case */
	{"INFO",	"INFO",	fs_cmd_info,	},
	{"LIB",		"LIB",	fs_cmd_lib,	},
	{"PASS",      	"PASS",	fs_cmd_pass,	},
	{"SDISC",      	"SDIS",	fs_cmd_sdisc,	},
};

#define NCMDS (sizeof(cmd_tab) / sizeof(cmd_tab[0]))

void fs_cli __P((struct fs_context *));

/*
 * Handle a command-line packet from a client.  This is rarely used by
 * new clients (but NetFS still uses *I am).
 */
void
fs_cli(c)
	struct fs_context *c;
{
	int i;
	char *head, *tail, *backup;
	c->req->data[strcspn(c->req->data, "\r")] = '\0';
	if (debug) printf("cli: [%s]", c->req->data);

	tail = c->req->data;
	backup = strdup(tail);
	if (*tail == '*') tail++;
	head = fs_cli_getarg(&tail);
	for (i = 0; i < NCMDS; i++) {
		if (fs_cli_match(head, &(cmd_tab[i]))) {
			(cmd_tab[i].impl)(c, tail);
			break;
		}
	}
	if (i == NCMDS)
		fs_cli_unrec(c, backup);
	free(backup);
}

static void
fs_cli_unrec(c, cmd)
	struct fs_context *c;
	char *cmd;
{
	struct ec_fs_reply *reply;

	reply = malloc(sizeof(*reply) + strlen(cmd) + 1);
	reply->command_code = EC_FS_CC_UNREC;
	reply->return_code = EC_FS_RC_OK;
	strcpy(reply->data, cmd);
	reply->data[strlen(cmd)] = '\r';
	fs_reply(c, reply, sizeof(*reply) + strlen(cmd) + 1);
	free(reply);
}

/*
 * Work out if word is an acceptable abbreviation for cmd.  Mutilates
 * word in the process.
 */

static int
fs_cli_match(word, cmd)
	char *word;
	const struct fs_cmd *cmd;
{
	char *p;
	for (p = word; *p!='\0'; p++)
		*p = toupper((unsigned char)*p);
	p--;
	if (*p == '.') {
		/* Abbreviated command */
		*p = '\0';
		if (strstr(cmd->full, word) == cmd->full &&
		    strstr(word, cmd->min) == word)
			return 1;
	} else {
		if (strcmp(word, cmd->full) == 0)
			return 1;
	}
	return 0;
}

/*
 * A bit like strsep, only different.  Breaks off the first word
 * (vaguely defined) of the input.  Afterwards, returns a pointer to
 * the first word (null-terminated) and points stringp at the start of
 * the tail.  Destroys the input in the process.
 */

char *
fs_cli_getarg(stringp)
	char **stringp;
{
	char *start;
	/* Skip leading whitespace */
	for (; **stringp == ' '; (*stringp)++);
	switch (**stringp) {
	case '"':
   		/* Quoted string. */
                /*
		 * XXX There seems to be no way to embed double quotes
		 * in a quoted string (or at least, NetFiler doesn't
		 * know how).  For now, assume the first '"' ends the
		 * string.
		 */
		(*stringp)++;
		start = *stringp;
		*stringp = strchr(start, '"');
		if (*stringp == NULL)
			/* Badness -- unterminated quoted string. */
			*stringp = strchr(start, '\0');
		else {
			**stringp = '\0';
			(*stringp)++;
		}
		break;
	case '\0':
		/* End of string hit.  Return two null strings. */
		start = *stringp;
		break;
	default:
		/* Unquoted.  Terminates at next space or end. */
		start = *stringp;
		*stringp = strchr(start, ' ');
		if (*stringp == NULL)
			*stringp = strchr(start, '\0');
		else {
			**stringp = '\0';
			(*stringp)++;
		}
	}
	return start;
}

static void
fs_cmd_i_am(c, tail)
	struct fs_context *c;
	char *tail;
{
	/* FIXME user authentication would be nice */
	struct ec_fs_reply_logon reply;
	char *login, *password, *oururd;
	if (strcasecmp(fs_cli_getarg(&tail), "am")) {
		fs_unrec(c);
		return;
	}
	login = fs_cli_getarg(&tail);
	password = fs_cli_getarg(&tail);
	if (debug) printf(" -> log on [%s:%s]\n", login, password);
	if (pwfile) {
		/*
		 * If no universal user root directory is set up, we
		 * instead validate user logins based on a password
		 * file, and look up the root directory for the
		 * given user.
		 */
		oururd = pw_validate(login, password);
		if (!oururd) {
			fs_err(c, EC_FS_E_BADPW);
			return;
		}
	} else {
		assert(fixedurd);
		oururd = strdup(fixedurd);
	}
	/*
	 * They're authenticated, so add them to the list of clients.
	 * First, we see if this client's already logged on, and if
	 * so, log them off first.
	 */
	if (c->client)
		fs_delete_client(c->client);
	c->client = fs_new_client(c->from);
	if (c->client == NULL) {
		fs_error(c, 0xff, "Internal server error");
		return;
	}
	c->client->login = strdup(login);
	c->client->urd = oururd;
	reply.std_tx.command_code = EC_FS_CC_LOGON;
	reply.std_tx.return_code = EC_FS_RC_OK;
	/* Initial user environment.  Note that we can't use the same handle twice. */
	reply.urd = fs_open_handle(c->client, oururd);
	reply.csd = fs_open_handle(c->client, oururd);
	reply.lib = fs_open_handle(c->client, lib);
	reply.opt4 = opt4;
	if (debug) printf("returning: urd=%d, csd=%d, lib=%d, opt4=%d\n",
			  reply.urd, reply.csd, reply.lib, reply.opt4);
	fs_reply(c, &(reply.std_tx), sizeof(reply));
}

static void
fs_cmd_pass(c, tail)
	struct fs_context *c;
	char *tail;
{
	struct ec_fs_reply reply;
	char *oldpw, *newpw, *oururd;
	oldpw = fs_cli_getarg(&tail);
	newpw = fs_cli_getarg(&tail);
	if (debug) printf(" -> change password\n");
	if (c->client == NULL) {
		fs_error(c, 0xff, "Who are you?");
		return;
	}
	if (pwfile) {
		if (!pw_change(c->client->login, oldpw, newpw)) {
			fs_err(c, EC_FS_E_BADPW);
			return;
		}
	} else {
		fs_err(c, EC_FS_E_LOCKED);
		return;
	}
	reply.command_code = EC_FS_CC_DONE;
	reply.return_code = EC_FS_RC_OK;
	fs_reply(c, &reply, sizeof(reply));
}

static void
fs_cmd_sdisc(c, tail)
	struct fs_context *c;
	char *tail;
{
	struct ec_fs_reply_sdisc reply;

	if (debug) printf(" -> sdisc\n");
	if (c->client == NULL) {
		fs_error(c, 0xff, "Who are you?");
		return;
	}
	reply.std_tx.command_code = EC_FS_CC_LOGON;
	reply.std_tx.return_code = EC_FS_RC_OK;
	/* Reset user environment.  Note that we can't use the same handle twice. */
	fs_close_handle(c->client, c->req->urd);
	fs_close_handle(c->client, c->req->csd);
	fs_close_handle(c->client, c->req->lib);
	reply.urd = fs_open_handle(c->client, "/home/bjh21");
	reply.csd = fs_open_handle(c->client, "/home/bjh21");
	reply.lib = fs_open_handle(c->client, "/");
	fs_reply(c, &(reply.std_tx), sizeof(reply));
}

static void
fs_cmd_lib(c, tail)
	struct fs_context *c;
	char *tail;
{
	char *upath;
	struct stat st;
	struct ec_fs_reply_dir reply;

	upath = fs_unixify_path(c, fs_cli_getarg(&tail)); /* Free it! */
	if (fs_stat(upath, &st) == -1) {
		fs_errno(c);
		goto burn;
	}
	fs_close_handle(c->client, c->req->lib);
	reply.new_handle = fs_open_handle(c->client, upath);
	if (reply.new_handle == 0) {
		fs_err(c, EC_FS_E_MANYOPEN);
		goto burn;
	}
	reply.std_tx.command_code = EC_FS_CC_LIB;
	reply.std_tx.return_code = EC_FS_RC_OK;
	fs_reply(c, &(reply.std_tx), sizeof(reply));
burn:
	free(upath);
}

static void
fs_cmd_info(c, tail)
	struct fs_context *c;
	char *tail;
{
	char *upath;
	struct stat st;
	struct ec_fs_reply *reply;
	char mode_buf[12];
	char *frag1, *frag2;

	/*
	 * XXX
	 *
	 * According to the RISC OS 3 PRM, this should return (in ASCII):
	 * Byte  Meaning
	 * 1-2   standard Rx Header (command code = 4)
	 * 3-12  object name, padded with spaces
	 * 13    space
	 * 14-21 load address, padded with zeros
	 * 22    space
	 * 23-30 execution address, padded with zeros
	 * 31-33 spaces
	 * 34-39 length padded with zeros
	 * 40-42 spaces
	 * 43-48 access details (eg LWR/WR), padded with spaces
	 * 49-53 spaces
	 * 54-61 date (DD:MM:YY)
	 * 62    space
	 * 63-68 System Internal Name (SIN), padded with zeros
	 * 69    Terminating negative byte (&80)
	 *
	 * awServer puts all numeric values in hex.
	 */

	upath = fs_unixify_path(c, fs_cli_getarg(&tail)); /* Free it! */
	if (lstat(upath, &st) == -1) {
		fs_errno(c);
		goto burn;
	}

	strmode(st.st_mode, mode_buf);
	asprintf(&frag1, "%s %3lu %-*s %-*s ", mode_buf,
	    (unsigned long)st.st_nlink,
	    UT_NAMESIZE, user_from_uid(st.st_uid, 0),
	    UT_NAMESIZE, group_from_gid(st.st_gid, 0));
        if (S_ISCHR(st.st_mode) || S_ISBLK(st.st_mode))
                asprintf(&frag2, "%3d,%5d ", major(st.st_rdev),
		    minor(st.st_rdev));
        else
                asprintf(&frag2, "%9qd ", (long long)st.st_size);
	reply = malloc(sizeof(reply) + strlen(frag1) + strlen(frag2)
	    + 13 + strlen(basename(upath)) + 1);
	strcpy(reply->data, frag1);
	strcat(reply->data, frag2);
	strcat(reply->data, printtime(st.st_mtime));
	strcat(reply->data, basename(upath));
	strcat(reply->data, "\x80");
	reply->command_code = EC_FS_CC_INFO;
	reply->return_code = EC_FS_RC_OK;
	fs_reply(c, reply, sizeof(*reply) + strlen(reply->data));
	free(frag1);
	free(frag2);
burn:
	free(upath);
}

static char *
printtime(ftime)
	time_t ftime;
{
	int i, j;
	char *longstring;
	static char shortstring[14];

	j = 0;
	longstring = ctime(&ftime);
	for (i = 4; i < 11; ++i)
		shortstring[j++] = longstring[i];

#define	SIXMONTHS	(365*24*60*60/2)
	if (ftime + SIXMONTHS > time((time_t *)NULL))
		for (i = 11; i < 16; ++i)
			shortstring[j++] = longstring[i];
	else {
		shortstring[j++] = ' ';
		for (i = 20; i < 24; ++i)
			shortstring[j++] = longstring[i];
	}
	shortstring[j++] = ' ';
	shortstring[j++] = 0;
	return shortstring;
}

