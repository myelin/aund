/* $NetBSD: fileserver.h,v 1.1 2001/02/06 23:54:46 bjh21 Exp $ */
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

#ifndef _FILESERVER_H
#define _FILESERVER_H

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/stat.h>
#include <netinet/in.h>

#include <dirent.h>
#include <fts.h>
#include <stdio.h>

#include "aun.h"
#include "fs_proto.h"

struct fs_context {
	struct ec_fs_req *req;		/* Request being handled */
	size_t req_len;			/* Size of request */
	struct aun_srcaddr *from;	/* Source of request */
	struct fs_client *client;	/* Pointer to client structure, or NULL if not logged in */
};

enum fs_handle_type { FS_HANDLE_FILE, FS_HANDLE_DIR };

struct fs_handle {
	char	*path;
	enum 	fs_handle_type type;
	int	fd; /* Only for files at present */
	u_int8_t	sequence; /* ditto */
};

struct fs_dir_cache {
	char *path; /* Path for which this is a cache. */
	int start; /* position in the directory this list starts at */
	FTS *ftsp; /* Pass to fts_close to free f */
	FTSENT *f; /* Result of fts_children on path */
};

struct fs_client {
	LIST_ENTRY(fs_client) link;
	struct aun_srcaddr host;
	int nhandles;
	struct fs_handle **handles; /* array of handles for this client */
	char *login;
	char *urd;
	struct fs_dir_cache dir_cache;
};

LIST_HEAD(fs_client_head, fs_client);
extern struct fs_client_head fs_clients;

extern char *discname;
extern char *fixedurd;
extern char *pwfile;
extern char *lib;
extern int opt4; /* ditto */

extern void fs_unrec __P((struct fs_context *));
extern char *fs_cli_getarg __P((char **));
extern void fs_reply __P((struct fs_context *, struct ec_fs_reply *, size_t));

extern void fs_errno __P((struct fs_context *));
extern void fs_err __P((struct fs_context *, u_int8_t));
extern void fs_error __P((struct fs_context *, u_int8_t, const char *));

extern void fs_check_handles __P((struct fs_context *));
extern int fs_check_handle __P((struct fs_client *, int));
extern int fs_open_handle __P((struct fs_client *, char *, int));
extern void fs_close_handle __P((struct fs_client *, int));
extern int fs_alloc_handle __P((struct fs_client *));
extern void fs_free_handle __P((struct fs_client *, int));

extern struct fs_client *fs_new_client __P((struct aun_srcaddr *));
extern void fs_delete_client __P((struct fs_client *));
extern struct fs_client *fs_find_client __P((struct aun_srcaddr *));

extern char *strpad __P((char *, int, size_t));
extern u_int8_t fs_mode_to_type __P((mode_t));
extern u_int8_t fs_mode_to_access __P((mode_t));
extern mode_t fs_access_to_mode __P((unsigned char, int));
extern char *fs_access_to_string __P((char *, u_int8_t));
extern u_int64_t fs_read_val __P((u_int8_t *, size_t));
extern void fs_write_val __P((u_int8_t *, u_int64_t, size_t));
extern u_int64_t fs_riscos_date __P((time_t));
extern void fs_get_meta __P((FTSENT *, struct ec_fs_meta *));
extern void fs_write_date __P((struct ec_fs_date *, time_t));
extern int fs_stat __P((const char *, struct stat *));
extern const char *fs_leafname __P((const char *));

extern char *fs_acornify_name __P((char *));
extern char *fs_unixify_path __P((struct fs_context *, char *));

extern int fs_guess_type __P((FTSENT *));
extern int fs_add_typemap_name __P((const char *, int));
extern int fs_add_typemap_mode __P((mode_t, mode_t, int));
extern int fs_add_typemap_default __P((int));

#endif
