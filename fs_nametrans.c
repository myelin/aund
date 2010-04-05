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
static void fs_match_path __P((char *));
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
	int nnames;
	char *csd, *lib;
	size_t disclen;
	char *path2;
	char *path3;
	char *p, *q;

	csd = c->req->csd ? c->client->handles[c->req->csd]->path : NULL;
	lib = c->req->lib ? c->client->handles[c->req->lib]->path : NULL;
	/*
	 * Plenty of space.
	 */
	path2 = malloc(strlen(c->client->urd) + (csd ? strlen(csd) : 0) +
		       (lib ? strlen(lib) : 0) +
		       2 * strlen(path) + 100);

	if (debug) printf("fs_unixify_path: [%s]", path);

	/*
	 * Skip disc name if one is supplied.  It would be better to
	 * check it for correctness too.
	 */
	if (path[0] == ':') {
		path++;
		disclen = strcspn(path, ".");
		/* 
		 * if (disclen != strlen(discname)) return NULL;
		 * if (strncmp(path, discname, disclen) != 0) return NULL;
		 */
		path += disclen;
		if (*path) path++;
	}
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
			base = csd; break;
		    case '%':
			base = lib; break;
		}
		path++;
		if (*path) path++;
	} else {
		base = csd;
	}
	if (base) {
		sprintf(path2, "%s/", base);
	} else {
		*path2 = '\0';
	}

	/*
	 * Append the supplied pathname to that prefix, performing
	 * simple translations on the way.
	 */
	fs_trans_simple(path2 + strlen(path2), path);

	if (debug) printf("->[%s]", path2);

	/*
	 * Unhat.
	 */
	fs_unhat_path(path2);

	if (debug) printf("->[%s]", path2);

	/*
	 * References directly to the root dir: turn an empty name
	 * into ".".
	 */
	if (!*path2)
		strcpy(path2, ".");

	/*
	 * Process every path component through fs_match_path.
	 */
	for (p = path2, nnames = 1; *p; p++)
		if (*p == '/')
			nnames++;
	path3 = malloc(20 * nnames + 10);
	p = path2;
	q = path3;
	while (*p) {
		char *r = p;
		while (*p && *p != '/') p++;
		sprintf(q, "%.*s", p-r, r);
		fs_match_path(path3);
		q += strlen(q);
		if (*p) {
			p++;
			*q++ = '/';
		}
	}
	if (debug) printf("->[%s]\n", path3);

	free(path2);
	path3 = realloc(path3, 1 + strlen(path3));

	return path3;
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
 * Case-insensitively match a file name against a potential
 * wildcard.
 */
static int wcfrag(char *frag, char *file)
{
	while (*frag && *frag != '*') {
		if (*frag != '?' && (toupper((unsigned char)*file) !=
				     toupper((unsigned char)*frag)))
			return 0;
		frag++;
		file++;
	}
	return 1;
}
static int wcmatch(char *wc, char *file, int len)
{
	char *fragend;
	char *filestart = file;
	int at_start = 1;

	while (*wc) {
		for (fragend = wc; *fragend && *fragend != '*'; fragend++);
		if (*fragend) {
			/*
			 * This fragment isn't the end of the
			 * wildcard, so we match it at the first
			 * place we can.
			 */
			while (len >= fragend - wc &&
			       ((at_start && file!=filestart) ||
				!wcfrag(wc, file)))
				file++, len--;
			if (len < fragend - wc)
				return 0;
			file += fragend - wc;
			len -= fragend - wc;
			wc = fragend;
		} else {
			/*
			 * This fragment is at the end, so we must
			 * match it at precisely the end or fail.
			 */
			if (len < fragend - wc)
				return 0;
			return ((!at_start || file==filestart) &&
				wcfrag(wc, file + len - (fragend - wc)));
		}
		while (*wc == '*') wc++;
		at_start = 0;
	}
	return 1;
}

/*
 * Find the real file that matches the name in 'path'. This may
 * involve:
 *
 *  - case-insensitively matching
 *  - wildcard matching (we just return the first match)
 *  - appending ,??? for a RISC OS file type
 */
static void
fs_match_path(path)
	char *path;
{
	struct stat st;
	char *pathcopy, *parentpath, *leaf, *wc;
	DIR *parent;
	struct dirent *dp;
	size_t leaflen;

	if (lstat(path, &st) == -1 && errno == ENOENT) {
		pathcopy = strdup(path);
		parentpath = dirname(pathcopy);
		parent = opendir(parentpath);
		if (parent == NULL) {
			free(pathcopy);
			return;
		}
		leaf = strrchr(path, '/');
		if (leaf)
			leaf++;
		else
			leaf = path;
		leaflen = strlen(leaf);
		wc = leaf;
		if (wc[0] == '.' && wc[1] == '.' && wc[2] == '.')
			wc += 2;       /* un-dot-stuff wildcard */
		while ((dp = readdir(parent)) != NULL) {
			char *name = dp->d_name;
			int namelen = strlen(dp->d_name);
			if (name[0] == '.') {
				if (namelen >= 3 &&
				    name[1] == '.' && name[2] == '.') {
					name += 2;   /* un-dot-stuff */
				} else
					continue;    /* hidden file */
			}
			if (namelen >= 4 && dp->d_name[namelen-4] == ',')
				namelen -= 4;
			if (namelen <= 10 &&
			    wcmatch(leaf, dp->d_name, namelen)) {
				strcpy(leaf, dp->d_name);
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
