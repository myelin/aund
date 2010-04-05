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

#include <assert.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <libgen.h>
#include <stdlib.h>
#include <string.h>

#include "extern.h"
#include "fileserver.h"

static char *fs_unhat_path __P((char *));
static void fs_typecase_path __P((char *, int));
static void fs_trans_simple __P((char *, char *));

/*
 * Convert a leaf name to Acorn style for presenting to the client.
 * Converts in place.
 */
char *
fs_acornify_name(name)
	char *name;
{
	size_t len;
	char *p, *q;

	p = q = name;
	if (*p == '.' && p[1] == '.' && p[2] == '.')
		p += 2;	       /* un-dot-stuff */
	for (; *p; p++)
		*q++ = (*p == '.' ? '/' : *p); /* un-slash-dot-swap */
	len = q - name;
	if (len >= 4 && name[len-4] == ',')
		/* For now, assume all *,??? names are magic */
		name[len-4] = '\0';
	else
		name[len] = '\0';
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
	char *pathret;
	char *p;

	/*
	 * Plenty of space.
	 */
	pathret = malloc(strlen(c->client->urd) +
			 strlen(c->client->handles[c->req->csd]->path) +
			 strlen(c->client->handles[c->req->lib]->path) +
			 2 * strlen(path) + 100);

	if (debug) printf("fs_unixify_path: [%s]", path);

	/*
	 * Decide what base path this pathname is relative to, by
	 * spotting magic characters at the front. Without any, of
	 * course, it'll be relative to the csd.
	 */
	if (path[0] && strchr("$&%@", path[0]) &&
	    (!path[1] || path[1] == '.')) {
		switch (path[0]) {
		    case '$':
			base = NULL; break;
		    case '&':
			base = c->client->urd; break;
		    case '@':
			base = c->client->handles[c->req->csd]->path; break;
		    case '%':
			base = c->client->handles[c->req->lib]->path; break;
		}
		path++;
		if (*path) path++;
	} else {
		base = c->client->handles[c->req->csd]->path;
	}
	if (base) {
		sprintf(pathret, "%s/", base);
	} else {
		*pathret = '\0';
	}

	/*
	 * Append the supplied pathname to that prefix, performing
	 * simple translations on the way.
	 */
	fs_trans_simple(pathret + strlen(pathret), path);

	if (debug) printf("->[%s]", pathret);

	/*
	 * Unhat.
	 */
	fs_unhat_path(pathret);

	if (debug) printf("->[%s]", pathret);

	/*
	 * References directly to the root dir: turn an empty name
	 * into ".".
	 */
	if (!*pathret)
		strcpy(pathret, ".");

	/*
	 * Case-mangle every path component.
	 */
	p = pathret;
	while (*p) {
		int c;
		while (*p && *p != '/') p++;
		c = *p;
		*p = '\0';
		fs_typecase_path(pathret, !c);
		*p = c;
		if (*p) p++;
	}
	if (debug) printf("->[%s]\n", pathret);

	pathret = realloc(pathret, 1 + strlen(pathret));

	return pathret;
}

/*
 * Remove '/foo/^' constructs from a path
 */
static char *
fs_unhat_path(path)
	char *path;
{
	char *p, *q;

	/*
	 * p walks along the path as we read it; q walks along the
	 * same string as we write the transformed version.
	 */
	p = q = path;

	while (*p) {
		if (*p == '^' && (!p[1] || p[1] == '/')) {
			/*
			 * Hat component. Skip it, and backtrack q.
			 */
			p++;
			while (q > path && q[-1] != '/')
				q--;   /* backtrack over the previous word */
			if (q > path)
				q--;   /* and over the slash before it */
		} else {
			/*
			 * Non-hat component. Just copy it in.
			 */
			if (q > path)
				*q++ = '/';
			while (*p && *p != '/')
				*q++ = *p++;
		}
		if (*p) {
			assert(*p == '/');
			p++;
		}
	}
	*q = '\0';
	return path;
}

/*
 * If <pathname> doesn't exist, try <pathname>,??? or case-mangling
 */
static void
fs_typecase_path(path, type)
	char *path;
	int type;
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
			return;
		}
		leaf = basename(path);
		len = strlen(leaf);
		while ((dp = readdir(parent)) != NULL) {
			if (len < sizeof(dp->d_name) &&
			    dp->d_name[len] == '\0' &&
			    strcasecmp(dp->d_name, leaf) == 0) {
				strcpy(path + strlen(path) - len, dp->d_name);
				break;
                	}
                	if (type && len + 4 < sizeof(dp->d_name) &&
			    dp->d_name[len] == ',' &&
			    strncasecmp(dp->d_name, leaf, len) == 0 &&
			    strlen(dp->d_name + len) == 4) {
				strcpy(path + strlen(path) - len, dp->d_name);
				break;
                	}
		}
		closedir(parent);
		free(pathcopy);
	}
}

/*
 * Simple translations: exchange . and /, and stuff two extra dots
 * at the front of any pathname starting with a dot. (That protects
 * '.', '..' and '.Acorn'.)
 */
static void
fs_trans_simple(pathret, path)
	char *pathret;
	char *path;
{
	/*
	 * Loop over each pathname component.
	 */
	while (*path) {
		if (*path == '/') {
			*pathret++ = '.';
			*pathret++ = '.';
		}
		while (*path && *path != '.') {
			if (*path == '/')
				*pathret++ = '.';
			else
				*pathret++ = *path;
			path++;
		}
		if (*path) {
			path++;
			*pathret++ = '/';
		}
	}
	*pathret = '\0';
}
