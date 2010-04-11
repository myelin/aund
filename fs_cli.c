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

typedef void fs_cmd_impl(struct fs_context *, char *);

struct fs_cmd {
	char	*name;
	int	minlen;
	fs_cmd_impl	*impl;
};

static fs_cmd_impl fs_cmd_dir;
static fs_cmd_impl fs_cmd_i_am;
static fs_cmd_impl fs_cmd_info;
static fs_cmd_impl fs_cmd_lib;
static fs_cmd_impl fs_cmd_logoff;
static fs_cmd_impl fs_cmd_sdisc;
static fs_cmd_impl fs_cmd_pass;
static fs_cmd_impl fs_cmd_rename;

static int fs_cli_match(char *cmdline, char **tail, const struct fs_cmd *cmd);
static void fs_cli_unrec(struct fs_context *, char *);

static const struct fs_cmd cmd_tab[] = {
	{"DIR", 	3, fs_cmd_dir,		},
	{"INFO",	1, fs_cmd_info,		},
	{"I AM", 	2, fs_cmd_i_am,		},
	{"LIB",		3, fs_cmd_lib,		},
	{"LOGOFF",	3, fs_cmd_logoff,	},
	{"PASS",      	1, fs_cmd_pass,		},
	{"RENAME",      1, fs_cmd_rename,	},
	{"SDISC",      	3, fs_cmd_sdisc,	},
};

#define NCMDS (sizeof(cmd_tab) / sizeof(cmd_tab[0]))

void fs_cli(struct fs_context *);

/*
 * Handle a command-line packet from a client.  This is rarely used by
 * new clients (but NetFS still uses *I am).
 */
void
fs_cli(struct fs_context *c)
{
	int i;
	char *head, *tail, *backup;
	int len;
	c->req->data[strcspn(c->req->data, "\r")] = '\0';

	head = backup = strdup(c->req->data);
	if (*head == '*') head++;
	for (i = 0; i < NCMDS; i++) {
		if (fs_cli_match(head, &tail, &(cmd_tab[i]))) {
			/*
			 * We print a diagnostic of the command for
			 * all commands except *I AM and *PASS,
			 * which will print their own truncated
			 * diagnostics to avoid echoing passwords on
			 * the screen.
			 */
			if (debug &&
			    cmd_tab[i].impl != fs_cmd_i_am &&
			    cmd_tab[i].impl != fs_cmd_pass)
				printf("cli: [%s]", c->req->data);
			(cmd_tab[i].impl)(c, tail);
			break;
		}
	}
	if (i == NCMDS) {
		if (debug)
			printf("cli: [%s]", c->req->data);
		fs_cli_unrec(c, backup);
	}
	free(backup);
}

static void
fs_cli_unrec(struct fs_context *c, char *cmd)
{
	struct ec_fs_reply *reply;

	if (debug) printf(" -> <unrecognised>\n");
	reply = malloc(sizeof(*reply) + strlen(cmd) + 1);
	reply->command_code = EC_FS_CC_UNREC;
	reply->return_code = EC_FS_RC_OK;
	strcpy(reply->data, cmd);
	reply->data[strlen(cmd)] = '\r';
	fs_reply(c, reply, sizeof(*reply) + strlen(cmd) + 1);
	free(reply);
}

/*
 * Work out if cmdline starts with an acceptable abbreviation for
 * cmd. If it does, return the tail of the command beyond that.
 */

static int
fs_cli_match(char *cmdline, char **tail, const struct fs_cmd *cmd)
{
	int i;

	for (i = 0;; i++) {
		int creal = cmd->name[i];
		int cthis = toupper((unsigned char)cmdline[i]);

		if (creal == '\0') {   /* real command has just ended */
			if (strchr(" .^&@$%", cthis)) {   /* so has input */
				*tail = cmdline + i;
				return 1;
			} else {
				return 0;   /* no match */
			}
		}
		if (cthis == '.') {    /* input command is abbreviated */
			if (i < cmd->minlen)
				return 0;   /* too short an abbreviation */
			*tail = cmdline + i + 1;   /* skip the dot */
			return 1;      /* abbreviation which matches */
		}
		if (creal != cthis)
			return 0;      /* mismatched character */
	}
}

/*
 * A bit like strsep, only different.  Breaks off the first word
 * (vaguely defined) of the input.  Afterwards, returns a pointer to
 * the first word (null-terminated) and points stringp at the start of
 * the tail.  Destroys the input in the process.
 */

char *
fs_cli_getarg(char **stringp)
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
fs_cmd_i_am(struct fs_context *c, char *tail)
{
	struct ec_fs_reply_logon reply;
	char *login, *password, *oururd;
	int opt4 = default_opt4;

	login = fs_cli_getarg(&tail);
	password = fs_cli_getarg(&tail);
	if (debug) printf("cli: log on [%s]\n", login);
	if (pwfile) {
		/*
		 * If no universal user root directory is set up, we
		 * instead validate user logins based on a password
		 * file, and look up the root directory for the
		 * given user.
		 */
		oururd = pw_validate(login, password, &opt4);
		if (!oururd) {
			fs_err(c, EC_FS_E_WRONGPW);
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
	reply.urd = fs_open_handle(c->client, oururd, 1);
	reply.csd = fs_open_handle(c->client, oururd, 1);
	reply.lib = fs_open_handle(c->client, lib, 1);
	reply.opt4 = opt4;
	if (debug) printf("returning: urd=%d, csd=%d, lib=%d, opt4=%d\n",
			  reply.urd, reply.csd, reply.lib, reply.opt4);
	fs_reply(c, &(reply.std_tx), sizeof(reply));
}

static void
fs_cmd_pass(struct fs_context *c, char *tail)
{
	struct ec_fs_reply reply;
	char *oldpw, *newpw, *oururd;
	oldpw = fs_cli_getarg(&tail);
	newpw = fs_cli_getarg(&tail);
	if (debug) printf("cli: change password\n");
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
fs_cmd_rename(struct fs_context *c, char *tail)
{
	struct ec_fs_reply reply;
	struct ec_fs_meta meta;
	char *oldname, *newname;
	char *oldupath, *newupath;
	char *path_argv[2];
	FTS *ftsp;
	FTSENT *f;
	
	oldname = fs_cli_getarg(&tail);
	newname = fs_cli_getarg(&tail);
	if (debug) printf(" -> rename [%s,%s]\n", oldname, newname);
	if (c->client == NULL) {
		fs_error(c, 0xff, "Who are you?");
		return;
	}
	oldupath = fs_unixify_path(c, oldname);
	newupath = fs_unixify_path(c, newname);
	if (oldupath == NULL || newupath == NULL) {
		fs_err(c, EC_FS_E_NOMEM);
		return;
	}
	if (rename(oldupath, newupath) < 0) {
		fs_errno(c);
	} else {
		path_argv[0] = oldupath;
		path_argv[1] = NULL;
		ftsp = fts_open(path_argv, FTS_LOGICAL, NULL);
		f = fts_read(ftsp);
		fs_get_meta(f, &meta);
		fs_del_meta(f);
		fts_close(ftsp);

		path_argv[0] = newupath;
		path_argv[1] = NULL;
		ftsp = fts_open(path_argv, FTS_LOGICAL, NULL);
		f = fts_read(ftsp);
		fs_set_meta(f, &meta);
		fts_close(ftsp);

		reply.command_code = EC_FS_CC_DONE;
		reply.return_code = EC_FS_RC_OK;
		fs_reply(c, &reply, sizeof(reply));
	}
	free(oldupath);
	free(newupath);
}

static void
fs_cmd_sdisc(struct fs_context *c, char *tail)
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
	reply.urd = fs_open_handle(c->client, "", 1);
	reply.csd = fs_open_handle(c->client, "", 1);
	reply.lib = fs_open_handle(c->client, "", 1);
	fs_reply(c, &(reply.std_tx), sizeof(reply));
}

static void
fs_cmd_dir(struct fs_context *c, char *tail)
{
	char *upath;
	struct stat st;
	struct ec_fs_reply_dir reply;

	if (c->client == NULL) {
		fs_error(c, 0xff, "Who are you?");
		return;
	}
	upath = fs_cli_getarg(&tail);
	if (debug) printf(" -> dir [%s]\n", upath);
	if (!*upath)
		upath = "&";
	upath = fs_unixify_path(c, upath);
	if (fs_stat(upath, &st) == -1) {
		fs_errno(c);
		goto burn;
	}
	fs_close_handle(c->client, c->req->csd);
	reply.new_handle = fs_open_handle(c->client, upath, 1);
	if (reply.new_handle == 0) {
		fs_err(c, EC_FS_E_MANYOPEN);
		goto burn;
	}
	reply.std_tx.command_code = EC_FS_CC_DIR;
	reply.std_tx.return_code = EC_FS_RC_OK;
	fs_reply(c, &(reply.std_tx), sizeof(reply));
burn:
	free(upath);
}

static void
fs_cmd_lib(struct fs_context *c, char *tail)
{
	char *upath;
	struct stat st;
	struct ec_fs_reply_dir reply;

	if (c->client == NULL) {
		fs_error(c, 0xff, "Who are you?");
		return;
	}
	upath = fs_cli_getarg(&tail);
	if (debug) printf(" -> lib [%s]\n", upath);
	upath = fs_unixify_path(c, upath); /* Free it! */
	if (fs_stat(upath, &st) == -1) {
		fs_errno(c);
		goto burn;
	}
	fs_close_handle(c->client, c->req->lib);
	reply.new_handle = fs_open_handle(c->client, upath, 1);
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
fs_cmd_logoff(struct fs_context *c, char *tail)
{
	/*
	 * This is an SJism, apparently; the vanilla Beeb command is
	 * *BYE, which is interpreted locally and converted into
	 * EC_FS_FUNC_LOGOFF. But SJ file servers supported
	 * OSCLI("LOGOFF") too.
	 *
	 * (Actually, documentation on the web suggests that *LOGOFF
	 * was mostly an SJ _administrator_ command, used to
	 * forcibly log off other users' sessions. But it worked
	 * without arguments as an unprivileged command synonymous
	 * with *BYE, and that's the only part I've implemented
	 * here.)
	 */
	if (debug) printf(" -> logoff\n");
	fs_logoff(c);
}

void
fs_long_info(char *string, FTSENT *f)
{
	struct ec_fs_meta meta;
	struct tm tm;
	unsigned long load, exec;
	char accstring[8], accstr2[8];
	mode_t currumask;
	char *acornname;
	int entries;

	acornname = strdup(f->fts_name);
	fs_acornify_name(acornname);
	if (!*acornname)
		strcpy(acornname, "$");

	fs_access_to_string(accstring,
			    fs_mode_to_access(f->fts_statp->st_mode));

	tm = *localtime(&f->fts_statp->st_mtime);

	if (infoformat == FS_INFO_SJ) {
		/*
		 * These formats for *INFO are taken from the SJ
		 * Research file server manual.
		 *
		 * The two dates are supposed to be creation and
		 * modification respectively, but since Unix doesn't
		 * give true creation dates I just set them both to
		 * the same thing. (The time goes with the
		 * modification date.)
		 *
		 * The two three-digit hex numbers at the end of the
		 * line are the primary and secondary account
		 * numbers on the SJ file server that own the file.
		 * I might have tried to make up something in here
		 * from the Unix uid and gid, but it didn't seem
		 * worth it. Though if aund actually supported
		 * multiple users and enforced access control
		 * between them, that would be a different matter,
		 * of course.
		 */
		if (S_ISDIR(f->fts_statp->st_mode)) {
			currumask = umask(777);
			umask(currumask);
			fs_access_to_string(accstr2,
					    fs_mode_to_access(0777 & ~currumask));

			/*
			 * Count the entries in a subdirectory.
			 */
			{
				char *lastslash = strrchr(f->fts_accpath, '/');
				char *fullpath;
				char *path_argv[2];
				FTS *ftsp2;
				FTSENT *f2;

				if (lastslash)
					lastslash++;
				else
					lastslash = f->fts_accpath;
				fullpath = malloc((lastslash - f->fts_accpath) +
						  f->fts_namelen + 8 + 1);
				sprintf(fullpath, "%.*s/%s",
					lastslash - f->fts_accpath,
					f->fts_accpath, f->fts_name);
				path_argv[0] = fullpath;
				path_argv[1] = NULL;

				ftsp2 = fts_open(path_argv, FTS_LOGICAL, NULL);
				f2 = fts_read(ftsp2);
				f2 = fts_children(ftsp2, FTS_NAMEONLY);
				for (entries = 0; f2 != NULL; f2 = f2->fts_link) {
					if (fs_hidden_name(f2->fts_name))
						continue;      /* hidden file */
					entries++;          /* count this one */
				}
				fts_close(ftsp2);
			}

			sprintf(string, "%-10.10s  Entries=%-4dDefault=%-6.6s  "
				"%-6.6s  %02d%.3s%02d %02d%.3s%02d %02d:%02d 000 (000)\r\x80",
				acornname, entries, accstr2, accstring,
				tm.tm_mday,
				"janfebmaraprmayjunjulaugsepoctnovdec" + 3*tm.tm_mon,
				tm.tm_year % 100,
				tm.tm_mday,
				"janfebmaraprmayjunjulaugsepoctnovdec" + 3*tm.tm_mon,
				tm.tm_year % 100,
				tm.tm_hour,
				tm.tm_min);
		} else {
			fs_get_meta(f, &meta);
			load = fs_read_val(meta.load_addr, sizeof(meta.load_addr));
			exec = fs_read_val(meta.exec_addr, sizeof(meta.exec_addr));
			sprintf(string, "%-10.10s %08X %08X     %06X "
				"%-6.6s  %02d%.3s%02d %02d%.3s%02d %02d:%02d 000 (000)\r\x80",
				acornname, load, exec,
				f->fts_statp->st_size, accstring,
				tm.tm_mday,
				"janfebmaraprmayjunjulaugsepoctnovdec" + 3*tm.tm_mon,
				tm.tm_year % 100,
				tm.tm_mday,
				"janfebmaraprmayjunjulaugsepoctnovdec" + 3*tm.tm_mon,
				tm.tm_year % 100,
				tm.tm_hour,
				tm.tm_min);
		}
	} else {
		/*
		 * This is the format specified in the RISC OS PRM.
		 */
		fs_get_meta(f, &meta);
		load = fs_read_val(meta.load_addr, sizeof(meta.load_addr));
		exec = fs_read_val(meta.exec_addr, sizeof(meta.exec_addr));
		sprintf(string, "%-10.10s %08X %08X   %06X   "
			"%-6.6s     %02d:%02d:%02d %06x\x80",
			acornname, load, exec,
			f->fts_statp->st_size, accstring,
			tm.tm_mday,
			tm.tm_mon,
			tm.tm_year % 100,
			fs_get_sin(f));
	}

	free(acornname);
}

static void
fs_cmd_info(struct fs_context *c, char *tail)
{
	char *upath;
	struct ec_fs_reply *reply;
	char *path_argv[2];
	FTS *ftsp;
	FTSENT *f;

	if (c->client == NULL) {
		fs_error(c, 0xff, "Who are you?");
		return;
	}
	upath = fs_cli_getarg(&tail);
	if (debug) printf(" -> info [%s]\n", upath);
	upath = fs_unixify_path(c, upath); /* Free it! */

	path_argv[0] = upath;
	path_argv[1] = NULL;
	ftsp = fts_open(path_argv, FTS_LOGICAL, NULL);
	f = fts_read(ftsp);
	if (f->fts_info == FTS_ERR || f->fts_info == FTS_NS) {
		fs_errno(c);
		fts_close(ftsp);
		free(upath);
		return;
	}

	reply = malloc(sizeof(*reply) + 100);
	fs_long_info(reply->data, f);
	reply->command_code = EC_FS_CC_INFO;
	reply->return_code = EC_FS_RC_OK;
	fs_reply(c, reply, sizeof(*reply) + strlen(reply->data));

	free(reply);
	free(upath);
	fts_close(ftsp);
}
