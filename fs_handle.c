/* $NetBSD: fs_handle.c,v 1.2 2001/02/08 15:55:54 bjh21 Exp $ */
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

#include <sys/types.h>
#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

#include "fileserver.h"

#define MAX_HANDLES 256

/*
 * Check a client context for validity.  Zero invalid handles.
 */
void fs_check_handles(c)
	struct fs_context *c;
{
	switch (c->req->function) {
	default:
		c->req->urd = fs_check_handle(c->client, c->req->urd);
		/* FALLTHROUGH */
	case EC_FS_FUNC_LOAD:
	case EC_FS_FUNC_SAVE:
	case EC_FS_FUNC_GETBYTES:
	case EC_FS_FUNC_PUTBYTES:
		/* In these calls, the URD is replaced by a port number */
		c->req->csd = fs_check_handle(c->client, c->req->csd);
		c->req->lib = fs_check_handle(c->client, c->req->lib);
		/* FALLTHROUGH */
	case EC_FS_FUNC_GETBYTE:
	case EC_FS_FUNC_PUTBYTE:
		/* And these ones don't pass context at all. */
		break;
	}
}

/*
 * Check a handle for validity.  Return the handle if it's valid and 0
 * if it isn't.
 */
int fs_check_handle(client, h)
	struct fs_client *client;
	int h;
{
	if (client && h < client->nhandles && client->handles[h])
		return h;
	else
		return 0;
}

/*
 * Open a new handle for a client.  path gives the Unix path of the
 * file or directory to open.
 */
int fs_open_handle(client, path)
	struct fs_client *client;
	char *path;
{
	struct stat sb;
	char *newpath;
	int h;

	h = fs_alloc_handle(client);
	if (h == 0) return h;
	if (stat(path, &sb) == -1) {
		warn("fs_open_handle");
		fs_free_handle(client, h);
		return 0;
	}
	if (S_ISDIR(sb.st_mode))
		client->handles[h]->type = FS_HANDLE_DIR;
	else if (S_ISREG(sb.st_mode))
		/* FIXME: open the file here? */
		client->handles[h]->type = FS_HANDLE_FILE;
	else {
		warnx("fs_open_handle: tried to open something odd");
		fs_free_handle(client, h);
		errno = ENOENT;
		return 0;
	}
	newpath = client->handles[h]->path = malloc(strlen(path)+1);
	if (newpath == NULL) {
		warnx("fs_open_handle: malloc failed");
		fs_free_handle(client, h);
		errno = ENOMEM;
		return 0;
	}
	strcpy(newpath, path);
	if (newpath[strlen(newpath)-1] == '/')
		newpath[strlen(newpath)-1] = '\0';
	return h;
}

/*
 * Release a handle set up by fs_open_handle.
 */

void
fs_close_handle(client, h)
	struct fs_client *client;
	int h;
{
	if (h == 0) return;
	switch (client->handles[h]->type) {
	case FS_HANDLE_FILE:
		close(client->handles[h]->fd);
	        break;
	case FS_HANDLE_DIR:
		break;
	}
	free(client->handles[h]->path);
	fs_free_handle(client, h);
}

int
fs_alloc_handle(client)
	struct fs_client *client;
{
	int h;
	/*
	 * Try to find a free handle first.  Handle 0 is special, so
	 * skip it.
	 */
	for (h = 1; h < client->nhandles; h++) {
		if (client->handles[h] == NULL) break;
	}
	if (h == client->nhandles) {
		/* No free handles.  See if we can extend the table. */
		if (h < MAX_HANDLES) {
			/* Yep */
			int new_nhandles;
			void *new_handles;
			new_nhandles = client->nhandles + 1;
			if (new_nhandles > MAX_HANDLES) new_nhandles = MAX_HANDLES;
			new_handles = realloc(client->handles, new_nhandles * sizeof(struct fs_handle *));
			if (new_handles != NULL) {
				client->handles = new_handles;
				client->nhandles = new_nhandles;
			} else {
				warnx("fs_alloc_handle: realloc failed");
				return 0;
			}
		} else {
			/* No more handles. */
			return 0;
		}
	}
	client->handles[h] = malloc(sizeof(*(client->handles[h])));
	if (client->handles[h] == NULL) {
		warnx("fs: fs_alloc_handle: malloc failed");
		h = 0;
	}
	return h;
}

void
fs_free_handle(client, h)
	int h;
	struct fs_client *client;
{
	/* very simple */
	free(client->handles[h]);
	client->handles[h] = 0;
}

