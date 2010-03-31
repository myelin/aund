/* $NetBSD: fileserver.c,v 1.4 2009/01/02 23:33:04 bjh21 Exp $ */
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

#include <sys/param.h>
#include <sys/queue.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <err.h>
#include <errno.h>
#include <fts.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "aun.h"
#include "fs_proto.h"
#include "extern.h"
#include "fileserver.h"

typedef void fs_func_impl __P((struct fs_context *));
extern fs_func_impl fs_cli;
extern fs_func_impl fs_examine;
extern fs_func_impl fs_open;
extern fs_func_impl fs_close;
extern fs_func_impl fs_getbytes;
extern fs_func_impl fs_get_args;
extern fs_func_impl fs_set_args;
extern fs_func_impl fs_get_discs;
extern fs_func_impl fs_get_info;
extern fs_func_impl fs_get_uenv;
extern fs_func_impl fs_logoff;

struct fs_client_head fs_clients = LIST_HEAD_INITIALIZER(fs_clients);

char *discname;
char *urd = "/"; /* XXX */
int opt4 = 0;

void
fs_init()
{
	discname = malloc(MAXHOSTNAMELEN > 17 ? MAXHOSTNAMELEN : 17);
	if (gethostname(discname, MAXHOSTNAMELEN) == -1)
		err(1, "gethostname");
	/* Chomp at the first '.' or 16 characters, whichever is sooner. */
	discname[strcspn(discname, ".")] = '\0';
	discname[16] = '\0';
}

#if 0
static void dump_handles __P((struct fs_client *));

static void dump_handles(client)
	struct fs_client *client;
{
	if (debug && client) {
		int i;
		printf("handles: ");
		for (i=1; i<client->nhandles; i++) {
			if (client->handles[i])
				printf(" %p", client->handles[i]);
			else
				printf(" NULL");
		}
		printf("\n");
	}
}
#endif

void
file_server(pkt, len, from)
	struct aun_packet *pkt;
	ssize_t len;
	struct aun_srcaddr *from;
{
	struct fs_context cont;
	struct fs_context *c = &cont;

	c->req = (struct ec_fs_req *)pkt;
	c->req_len = len;
	c->from = from;
	c->client = fs_find_client(from);
	fs_check_handles(c);
	((char *)(c->req))[c->req_len] = '\0'; /* Null-terminate in case client is silly */

	switch (c->req->function) {
	case EC_FS_FUNC_CLI:
		fs_cli(c);
		break;
	case EC_FS_FUNC_EXAMINE:
		fs_examine(c);
		break;
	case EC_FS_FUNC_OPEN:
		fs_open(c);
		break;
	case EC_FS_FUNC_CLOSE:
		fs_close(c);
		break;
	case EC_FS_FUNC_GETBYTES:
		fs_getbytes(c);
		break;
	case EC_FS_FUNC_GET_ARGS:
		fs_get_args(c);
		break;
	case EC_FS_FUNC_SET_ARGS:
		fs_set_args(c);
		break;
	case EC_FS_FUNC_GET_DISCS:
		fs_get_discs(c);
		break;
	case EC_FS_FUNC_GET_INFO:
		fs_get_info(c);
		break;
	case EC_FS_FUNC_GET_UENV:
		fs_get_uenv(c);
		break;
	case EC_FS_FUNC_LOGOFF:
		fs_logoff(c);
		break;
	default:
		if (debug) printf("unknown function %d\n", c->req->function);
		/*fs_unrec(sock, request, from);*/
		fs_error(c, 0xff, "Not yet implemented!");
	}
}

void
fs_unrec(c)
	struct fs_context *c;
{
	struct ec_fs_reply reply;
	reply.command_code = EC_FS_CC_UNREC;
	reply.return_code = EC_FS_RC_OK;
	fs_reply(c, &reply, sizeof(reply));
}

void
fs_reply(c, reply, len)
	struct fs_context *c;
	struct ec_fs_reply *reply;
	size_t len;
{
	reply->aun.type = AUN_TYPE_UNICAST;
	reply->aun.dest_port = c->req->reply_port;
	reply->aun.flag = c->req->aun.flag;
	if (aunfuncs->xmit(&(reply->aun), len, c->from) == -1)
		warn("Tx reply");
}

struct fs_client *
fs_new_client(from)
	struct aun_srcaddr *from;
{
	struct fs_client *client;
	client = calloc(1, sizeof(*client));
	if (client == NULL) {
		warnx("fs_new_client: calloc failed");
		return NULL;
	}
	/*
	 * All clients have a null handle, handle 0.  We'll
	 * pre-allocate another three, since all clients get three
	 * handles allocated at login.
	 */
	client->handles = calloc(4, sizeof(struct fs_handle *));
	if (client->handles == NULL) {
		warnx("fs_new_client: calloc failed");
		free(client);
		return NULL;
	}
	client->nhandles = 4;
	client->host = *from;
	client->login = NULL;
	client->dir_cache.path = NULL;
	client->dir_cache.ftsp = NULL;
	client->dir_cache.f = NULL;
	LIST_INSERT_HEAD(&fs_clients, client, link);
	if (using_syslog)
		syslog(LOG_INFO, "login from %s", aunfuncs->ntoa(from));
	return client;
}

struct fs_client *
fs_find_client(from)
	struct aun_srcaddr *from;
{
	struct fs_client *c;
	for (c = fs_clients.lh_first; c != NULL; c = c->link.le_next)
		if (memcmp(from, &(c->host), sizeof(struct aun_srcaddr)) == 0)
			break;
	return c;
}

void
fs_delete_client(client)
	struct fs_client *client;
{
	int i;
	LIST_REMOVE(client, link);
	for (i=0; i < client->nhandles; i++)
		if (client->handles[i] != NULL)
			fs_close_handle(client, i);
	free(client->handles);
	free(client->login);
	if (client->dir_cache.ftsp)
		fts_close(client->dir_cache.ftsp);
	if (using_syslog)
		syslog(LOG_INFO, "logout from %s", aunfuncs->ntoa(&client->host));
	free(client);
}
