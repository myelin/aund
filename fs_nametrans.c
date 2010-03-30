/* $NetBSD: fs_nametrans.c,v 1.1 2001/02/06 23:54:46 bjh21 Exp $ */
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
 * fs_nametrans.c -- File-name translation (Unix<->Acorn)
 */

#include <sys/stat.h>
#include <sys/types.h>

#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <libgen.h>
#include <stdlib.h>
#include <string.h>

#include "extern.h"
#include "fileserver.h"

static char *fs_merge_paths __P((const char *, const char *));
static char *fs_get_base __P((struct fs_context *, char **));
static char *fs_unhat_path __P((char *));
static char *fs_type_path __P((char *));
#if 0
static char *fs_smash_case __P((const char *, char *));
#endif
static char *fs_trans_simple __P((char *));

/*
 * Convert a leaf name to Acorn style for presenting to the client.
 * Converts in place.
 */
char *
fs_acornify_name(name)
	char *name;
{
	size_t len;

	fs_trans_simple(name);
	len = strlen(name);
	if (len >= 4 && name[len-4] == ',')
		/* For now, assume all *,??? names are magic */
		name[len-4] = '\0';
	return name;
}

/*
 * Convert a path provided by a client into a Unix one.  Note that the
 * new path is in a freshly mallocked block, and the caller is
 * responsible for freeing it.
 */
char *
fs_unixify_path(c, path)
	struct fs_context *c;
	char *path;
{
	const char *base;

	if (debug) printf("fs_unixify_path: [%s]", path);
	/* t%./%/.% */
	fs_trans_simple(path);
	if (debug) printf("->[%s]", path);
	/* skip absolutes */
	base = fs_get_base(c, &path);
	if (debug) printf("->[%s,%s]", base, path);
	/* unhat */
	fs_unhat_path(path);
	if (debug) printf("->[%s,%s]", base, path);
	/* resolve absolutes (more unhatting if necessary, but '^'s in the stored path should be retained) */
	path = fs_merge_paths(base, path);
	if (debug) printf("->[%s]", path);
	/* add ",???" type indicator to end if necessary*/
	path = fs_type_path(path);
	if (debug) printf("->[%s]\n", path);
	return path;
}

/*
 * Concatenate two paths, removing "^/"s at the start of the second
 * and corresponding components at the end of the first.  Returns a
 * freshly malloced block.  Caller is responsible for freeing it.
 */
static char *
fs_merge_paths(base, path)
	const char *base, *path;
{
	int nhats, i;
	char *out, *new;

	nhats = 0;
	while (strlen(path) >= 2 && strncmp(path, "^/", 2) == 0) {
		nhats++;
		path +=2;
	}
	if (strcmp(path, "^") == 0) {
		nhats++;
		path++;
	}
	out = strdup(base);
	if (out == NULL)
		return NULL;
	for (i=0; i<nhats && strlen(out)>1; i++)
		*strrchr(out, '/') = '\0';
	new = realloc(out, strlen(out)+strlen(path)+2);
	if (new == NULL) {
		free(out);
		return NULL;
	}
	out = new;
	strcat(out, "/");
	strcat(out, path);
	return out;
}

/*
 * resolve an absolute section at the start of a path.  Returns the
 * 'base' path, and modifies the supplied pointer to point past any
 * absolute specifier it contained.
 */
static char *
fs_get_base(c, pathp)
	struct fs_context *c;
	char **pathp;
{
	char *path;
	size_t disclen;
	int h;

	path = *pathp;
	if (path[0] == ':') {
		disclen = strcspn(path+1, "/");
		if (disclen != strlen(discname)) return NULL;
		if (strncmp(path+1, discname, disclen) != 0) return NULL;
		path += disclen+2;
	}
	if (strlen(path) > 0 && strchr("$@%&", path[0]) && (path[1] == '/' || path[1] == '\0')) {
		/* Absolute path specified */
		switch (path[0]) {
		case '$': *pathp = path+2; return "";
		case '&': h = c->req->urd;
		case '@': h = c->req->csd;
		case '%': h = c->req->lib;
		default: h = 0; /* Keep gcc quiet */
		}
		path += 2;
	} else
		h = c->req->csd;
	*pathp = path;
	if (h)
		return c->client->handles[h]->path;
	else
		return "";
}

/*
 * Remove '/foo/^' constructs from a path
 */
static char *
fs_unhat_path(path)
	char *path;
{
	char *p, *q, *r;
	
	p = path;
	/* We can't resolve initial hats yet. */
	while (strncmp(p, "^/", 2) == 0)
		p += 2;
	while ((q = strstr(p, "/^/")) != 0) {
		r = q;
		while(*(r-1) != '/' && r > p)
			r--;
		/*
		 * r now points at the start of the path component to
		 * be excised
		 */
		strcpy(r, q + 3); /* XXX Is this safe???? */
	}
	/*
	 * Now just need to trim off a possible "foo/^" at the end of
	 * the path.
	 */
	if (strcmp(strchr(p, '\0') - 2, "/^") == 0) {
		r = strchr(p, '\0') - 3;
		while(*r != '/' && r > p)
			r--;
		/* r points at the slash before the doomed path component */
		*r = '\0';
	}
	return path;
}

/*
 * If <pathname> doesn't exist, try <pathname>,???
 * May realloc the path.
 */
static char *
fs_type_path(path)
	char *path;
{
	struct stat st;
	char *pathcopy, *parentpath, *leaf;
	DIR *parent;
	struct dirent *dp;
	size_t len;

	if (lstat(path, &st) == -1 && errno == ENOENT) {
		pathcopy = strdup(path);
		parentpath = dirname(pathcopy);
		parent = opendir(parentpath);
		if (parent == NULL) {
			free(pathcopy);
			return path;
		}
		leaf = basename(path);
		len = strlen(leaf);
		while ((dp = readdir(parent)) != NULL) {
                	if (len + 4 < sizeof(dp->d_name) &&
			    dp->d_name[len] == ',' &&
			    strncmp(dp->d_name, leaf, len) == 0 &&
			    strlen(dp->d_name + len) == 4) {
				path = realloc(path, strlen(path)+5);
				strcat(path, dp->d_name + len);
				break;
                	}
		}
		closedir(parent);
		free(pathcopy);
	}
	return path;
}

/*
 * Simple translation, swapping '.' and '/'.
 */
static char *
fs_trans_simple(path)
	char *path;
{
	int i;
	for (i = 0; path[i]; i++) {
		switch (path[i]) {
		case '/': path[i] = '.'; break;
		case '.': path[i] = '/'; break;
		}
	}
	return path;
}
