/* $NetBSD: fs_util.c,v 1.1 2001/02/06 23:54:46 bjh21 Exp $ */
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

#include <sys/errno.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fts.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "aun.h"
#include "extern.h"
#include "fs_proto.h"
#include "fileserver.h"

char *strpad(s, c, len)
	char *s;
	int c;
	size_t len;
{
	int i;
	for (i = strlen(s); i < len; i++)
		s[i] = c;
	return s;
}

u_int8_t
fs_mode_to_type(mode)
	mode_t mode;
{
	if (S_ISDIR(mode)) return EC_FS_TYPE_DIR;
	return EC_FS_TYPE_FILE;
	/* Old clients may prefer EC_FS_TYPE_SOME */
}

/*
 * Conversions between Acorn access and Unix modes.  Acorn 'L'
 * prevents an object being deleted and has no Unix equivalent.
 * usergroup determines whether group permissions follow user or
 * public permissions when changed by an Acorn client.
 */

u_int8_t
fs_mode_to_access(mode)
	mode_t mode;
{
	unsigned char access;
	access = 0;
	if (mode & S_IRUSR) access |= EC_FS_ACCESS_UR;
	if (mode & S_IWUSR) access |= EC_FS_ACCESS_UW;
	if (mode & S_IROTH) access |= EC_FS_ACCESS_OR;
	if (mode & S_IWOTH) access |= EC_FS_ACCESS_OW;
	if (S_ISDIR(mode))  access |= EC_FS_ACCESS_D;
	return access;
}

mode_t fs_access_to_mode(access, usergroup)
	unsigned char access;
	int usergroup;
{
	mode_t mode;
	mode = 0;
	if (access & EC_FS_ACCESS_UR) mode |= S_IRUSR | (usergroup ? S_IRGRP : 0);
	if (access & EC_FS_ACCESS_UW) mode |= S_IWUSR | (usergroup ? S_IWGRP : 0);
	if (access & EC_FS_ACCESS_OR) mode |= S_IROTH | (usergroup ? 0 : S_IRGRP);
	if (access & EC_FS_ACCESS_OW) mode |= S_IWOTH | (usergroup ? 0 : S_IWGRP);
	return mode;
}

char *
fs_access_to_string(buf, access)
	char *buf;
	u_int8_t access;
{
	buf[0] = '\0';
	if (access & EC_FS_ACCESS_D) strcat(buf, "D");
	if (access & EC_FS_ACCESS_L) strcat(buf, "L");
	if (access & EC_FS_ACCESS_UW) strcat(buf, "W");
	if (access & EC_FS_ACCESS_UR) strcat(buf, "R");
	strcat(buf, "/");
	if (access & EC_FS_ACCESS_OW) strcat(buf, "W");
	if (access & EC_FS_ACCESS_OR) strcat(buf, "R");
	return buf;
}

u_int64_t
fs_read_val(p, len)
	u_int8_t *p;
	size_t len;
{
	u_int64_t value, mask;

	value = 0;
	p += len - 1;
	while(len) {
		value <<= 8;
		value |= *p;
		p--;
		len--;
	}
	return value;
}

void
fs_write_val(p, value, len)
	u_int8_t *p;
	u_int64_t value;
	size_t len;
{
	u_int64_t max;

	max = (1ULL << (len * 8)) - 1;
	if (value > max) value = max;
	while (len) {
		*p = value & 0xff;
		p++;
		len--;
		value >>= 8;
	}
}

void
fs_get_meta(f, meta)
	FTSENT *f;
	struct ec_fs_meta *meta;
{
	struct stat *st, sb;
	char *metapath, rawinfo[24];
	u_int64_t stamp;
	int type, i;
	
	st = &sb;

	metapath = malloc(strlen(f->fts_accpath) + f->fts_namelen + 7 + 1);
	if (metapath != NULL) {
		strcpy(metapath, f->fts_accpath);
		strcat(metapath, ".Acorn/");
		strcat(metapath, f->fts_name);
 		rawinfo[23] = '\0';
		if (readlink(metapath, rawinfo, 23) == 23) {
			for (i = 0; i < 4; i++)
				meta->load_addr[i] = strtoul(rawinfo+i*3, NULL, 16);
			for (i = 0; i < 4; i++)
				meta->exec_addr[i] = strtoul(rawinfo+12+i*3, NULL, 16);
			return;
		}
	}
	st = f->fts_statp;
	if (st != NULL) {
		stamp = fs_riscos_date(st->st_mtime);
		type = fs_guess_type(f);
		fs_write_val(meta->load_addr,
			     0xfff00000 | (type << 8) | (stamp >> 32), 4);
		fs_write_val(meta->exec_addr, stamp & 0x00ffffffff, 4);
	} else {	
		fs_write_val(meta->load_addr, 0xdeaddead, 4);
		fs_write_val(meta->exec_addr, 0xdeaddead, 4);
	}
}

/*
 * Convert a Unix time_t (non-leap seconds since 1970-01-01) to a RISC
 * OS time (non-leap(?) centiseconds since 1900-01-01(?)).
 */

u_int64_t fs_riscos_date(time)
	time_t time;
{
	u_int64_t base;

	base = 31536000ULL * 70 + 86400 * 17;
	return (((u_int64_t)time) + base)*100;
}

/*
 * Convert a date stamp from Unix to Acorn fileserver.  Unfortunately,
 * the old file server format overflowed at the start of 1998.  I know
 * that Acorn filched some bits from elsewhere to add to the year, and
 * I think I've got this right.
 */
void
fs_write_date(date, time)
	struct ec_fs_date *date;
	time_t time;
{
	struct tm *t;
	int year81;
	t = localtime(&time);
	if (t->tm_year < 81) {
		/* Too early -- return lowest date possible */
		date->day = 1;
		date->year_month = 1;
	} else {
		year81 = t->tm_year - 81;
		date->day = t->tm_mday | ((year81 & 0xf0) << 1);
		date->year_month = (t->tm_mon + 1) | (year81 << 4);
	}
}

/*
 * Mostly like stat(2), but if called on a broken symlink, returns
 * information on the symlink itself.
 */
int
fs_stat(path, sb)
	const char *path;
	struct stat *sb;
{
	int rc;
	rc = stat(path, sb);
	if (rc == -1 && errno == ENOENT)
		/* Could be a broken symlink */
		rc = lstat(path, sb);
	return rc;
}

const char *
fs_leafname(path)
	const char *path;
{
	char *leaf;
	if ((leaf = strrchr(path,'/')) != NULL)
		return leaf+1;
	else
		return path;
}
