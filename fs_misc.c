/* $NetBSD: fs_misc.c,v 1.1 2001/02/06 23:54:46 bjh21 Exp $ */
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
 * fs_misc.c - miscellaneous file server calls
 */

#include <sys/types.h>
#include <sys/stat.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "aun.h"
#include "fs_proto.h"
#include "fs_errors.h"
#include "fileserver.h"
#include "extern.h"

void fs_get_discs __P((struct fs_context *));
void fs_get_info __P((struct fs_context *));
void fs_get_uenv __P((struct fs_context *));
void fs_logoff __P((struct fs_context *));

void
fs_get_discs(c)
	struct fs_context *c;
{
	struct ec_fs_reply_get_discs *reply;
	struct ec_fs_req_get_discs *request = (struct ec_fs_req_get_discs *)(c->req);
	int nfound;

	if (debug) printf("get discs [%d/%d]\n", request->sdrive, request->ndrives);
	if (request->sdrive == 0 && request->ndrives > 0)
		nfound = 1;
	else
		nfound = 0;
	if ((reply = malloc(SIZEOF_ec_fs_reply_discs(nfound))) == NULL) exit(2);
	reply->std_tx.command_code = EC_FS_CC_DISCS;
	reply->std_tx.return_code = EC_FS_RC_OK;
	reply->ndrives = nfound;
	if (nfound) {
		reply->drives[0].num = 0;
		strncpy(reply->drives[0].name, discname, sizeof(reply->drives[0].name));
		strpad(reply->drives[0].name, ' ', sizeof(reply->drives[0].name));
	}
	fs_reply(c, &(reply->std_tx), SIZEOF_ec_fs_reply_discs(nfound));
	free(reply);
}

void
fs_get_info(c)
	struct fs_context *c;
{
	char *upath, *path_argv[2];
	struct ec_fs_req_get_info *request;
	FTS *ftsp;
	FTSENT *f;

	if (c->client == NULL) {
		fs_err(c, EC_FS_E_WHOAREYOU);
		return;
	}
	request = (struct ec_fs_req_get_info *)c->req;
	request->path[strcspn(request->path, "\r")] = '\0';
	if (debug) printf("get info [%d, %s]\n", request->arg, request->path);
	upath = fs_unixify_path(c, request->path); /* This must be freed */
	if (upath == NULL) {
		fs_err(c, EC_FS_E_NOMEM);
		return;
	}
	errno = 0;
	path_argv[0] = upath;
	path_argv[1] = NULL;
	ftsp = fts_open(path_argv, FTS_LOGICAL, NULL);
	f = fts_read(ftsp);
	switch (request->arg) {
	case EC_FS_GET_INFO_ACCESS: {
		struct ec_fs_reply_info_access reply;
		reply.std_tx.return_code = EC_FS_RC_OK;
		reply.std_tx.command_code = EC_FS_CC_DONE;
		if (f->fts_info == FTS_ERR || f->fts_info == FTS_NS) {
			reply.type = 0xff;
		} else {
			reply.type = fs_mode_to_type(f->fts_statp->st_mode);
			reply.access = fs_mode_to_access(f->fts_statp->st_mode);
		}
		fs_reply(c, &(reply.std_tx), sizeof(reply));
	}
	break;
	case EC_FS_GET_INFO_ALL: {
		struct ec_fs_reply_info_all reply;
		
		reply.std_tx.return_code = EC_FS_RC_OK;
		reply.std_tx.command_code = EC_FS_CC_DONE;
		if (f->fts_info == FTS_ERR || f->fts_info == FTS_NS) {
			reply.type = 0;
		} else {
			reply.type = fs_mode_to_type(f->fts_statp->st_mode);
			fs_get_meta(f, &(reply.meta));
			fs_write_val(reply.size, f->fts_statp->st_size, sizeof(reply.size));
			reply.access = fs_mode_to_access(f->fts_statp->st_mode);
			fs_write_date(&(reply.date), f->fts_statp->st_ctime);
		}
		fs_reply(c, &(reply.std_tx), sizeof(reply));
	}
	break;
	case EC_FS_GET_INFO_DIR:
	{
		struct ec_fs_reply_info_dir reply;
		
		if (f->fts_info == FTS_ERR || f->fts_info == FTS_NS) {
			fs_errno(c);
			fts_close(ftsp);
			return;
		}
		reply.std_tx.return_code = EC_FS_RC_OK;
		reply.std_tx.command_code = EC_FS_CC_DONE;
		reply.undef0 = 0;
		reply.zero = 0;
		reply.ten = 10;
		fs_acornify_name(f->fts_name);
		if (f->fts_name[0] == '\0') strcpy(f->fts_name, "$");
		strncpy(reply.dir_name, f->fts_name, sizeof(reply.dir_name));
		strpad(reply.dir_name, ' ', sizeof(reply.dir_name));
		reply.dir_access = FS_DIR_ACCESS_PUBLIC; /* XXX should check */
		reply.cycle = 0; /* XXX should fake */
		fs_reply(c, &(reply.std_tx), sizeof(reply));
	}
	break;
	default:
		fs_err(c, EC_FS_E_BADINFO);
	}
	fts_close(ftsp);
	free(upath);
}

void
fs_get_uenv(c)
	struct fs_context *c;
{
	struct ec_fs_reply_get_uenv reply;
	char tmp[11];

	if (debug) printf("get user environment\n");
	reply.std_tx.command_code = EC_FS_CC_DONE;
	reply.std_tx.return_code = EC_FS_RC_OK;
	reply.discnamelen = sizeof(reply.csd_discname);
	strncpy(reply.csd_discname, discname, sizeof(reply.csd_discname));
	strpad(reply.csd_discname, ' ', sizeof(reply.csd_discname));
	tmp[10] = '\0';
	if (c->req->csd) {
		strncpy(tmp, fs_leafname(c->client->handles[c->req->csd]->path), sizeof(tmp) - 1);
		fs_acornify_name(tmp);
		if (tmp[0] == '\0') strcpy(tmp, "$");
	}
	else
		tmp[0] = '\0';
	strncpy(reply.csd_leafname, tmp, sizeof(reply.csd_leafname));
	strpad(reply.csd_leafname, ' ', sizeof(reply.csd_leafname));
	if (c->req->lib) {
		strncpy(tmp, fs_leafname(c->client->handles[c->req->lib]->path), sizeof(tmp) - 1);
		fs_acornify_name(tmp);
		if (tmp[0] == '\0') strcpy(tmp, "$");
	}
	else
		tmp[0] = '\0';
	strncpy(reply.lib_leafname, tmp, sizeof(reply.lib_leafname));
	strpad(reply.lib_leafname, ' ', sizeof(reply.lib_leafname));
	fs_reply(c, &(reply.std_tx), sizeof(reply));
}

void
fs_logoff(c)
	struct fs_context *c;
{
	struct ec_fs_reply reply;

	if (debug) printf ("log off\n");
	if (c->client != NULL)
		fs_delete_client(c->client);
	reply.command_code = EC_FS_CC_DONE;
	reply.return_code = EC_FS_RC_OK;
	fs_reply(c, &reply, sizeof(reply));
}
