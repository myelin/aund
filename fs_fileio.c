/* $NetBSD: fs_fileio.c,v 1.2 2009/01/02 23:33:04 bjh21 Exp $ */
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
 * fs_fileio.c - File server file I/O calls
 */

#include <sys/types.h>
#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "aun.h"
#include "fs_proto.h"
#include "fs_errors.h"
#include "extern.h"
#include "fileserver.h"

#define OUR_DATA_PORT 0x97

void fs_open __P((struct fs_context *));
void fs_close __P((struct fs_context *));
void fs_get_args __P((struct fs_context *));
void fs_set_args __P((struct fs_context *));
void fs_getbytes __P((struct fs_context *));

static ssize_t fs_data_send __P((struct fs_context *, int, size_t));
static ssize_t fs_data_recv __P((struct fs_context *, int, size_t, int));

void
fs_open(c)
	struct fs_context *c;
{
	struct ec_fs_reply_open reply;
	struct ec_fs_req_open *request;
	char *upath;
	int openopt, fd;
	u_int8_t h;

	if (c->client == NULL) {
		fs_err(c, EC_FS_E_WHOAREYOU);
		return;
	}
	request = (struct ec_fs_req_open *)(c->req);
	request->path[strcspn(request->path, "\r")] = '\0';
	if (debug) printf("open [%s/%s, %s]\n", request->must_exist ? "exist":"create",
			  request->read_only ? "read":"rdwr", request->path);
	upath = fs_unixify_path(c, request->path);
	if (upath == NULL) {
		fs_err(c, EC_FS_E_NOMEM);
		return;
	}
	if ((h = fs_open_handle(c->client, upath, request->must_exist)) == 0) {
		fs_errno(c);
		free(upath);
		return;
	}
	openopt = 0;
	if (!request->must_exist) openopt |= O_CREAT;
	if (request->read_only)
		openopt |= O_RDONLY;
	else
		openopt |= O_RDWR;
	if ((fd = open(upath, openopt, 0666)) == -1) {
		fs_errno(c);
		fs_close_handle(c->client, h);
		free(upath);
		return;
	}
	c->client->handles[h]->fd = fd;
	reply.std_tx.command_code = EC_FS_CC_DONE;
	reply.std_tx.return_code = EC_FS_RC_OK;
	reply.handle = h;
	fs_reply(c, &(reply.std_tx), sizeof(reply));
	free(upath);
}

void
fs_close(c)
	struct fs_context *c;
{
	struct ec_fs_reply reply;
	struct ec_fs_req_close *request;
	int h, fd, rc;
	
	if (c->client == NULL) {
		fs_err(c, EC_FS_E_WHOAREYOU);
		return;
	}
	request = (struct ec_fs_req_close *)(c->req);
	if (debug) printf("close [%d]\n", request->handle);
	if ((h = fs_check_handle(c->client, request->handle)) != 0) {
		fd = c->client->handles[request->handle]->fd;
		if ((rc = fsync(fd)) == -1) /* ESUG says this is needed */
			fs_errno(c);
		close(fd);
		fs_close_handle(c->client, h);
	} else
		rc = 0;
	if (rc == 0) {
		reply.command_code = EC_FS_CC_DONE;
		reply.return_code = EC_FS_RC_OK;
		fs_reply(c, &reply, sizeof(reply));
	}
}

void
fs_get_args(c)
	struct fs_context *c;
{
	struct stat st;
	struct ec_fs_reply_get_args reply;
	struct ec_fs_req_get_args *request;
	off_t ptr;
	int h, fd;

	if (c->client == NULL) {
		fs_err(c, EC_FS_E_WHOAREYOU);
		return;
	}
	request = (struct ec_fs_req_get_args *)(c->req);
	if (debug) printf("get args [%d, %d]\n", request->handle, request->arg);
	if ((h = fs_check_handle(c->client, request->handle)) != 0) {
		fd = c->client->handles[h]->fd;
		switch (request->arg) {
		case EC_FS_ARG_PTR:
			if ((ptr = lseek(fd, 0, SEEK_CUR)) == -1) {
				fs_errno(c);
				return;
			}
			fs_write_val(reply.val, ptr, sizeof(reply.val));
			break;
		case EC_FS_ARG_EXT:
			if (fstat(fd, &st) == -1) {
				fs_errno(c);
				return;
			}
			fs_write_val(reply.val, st.st_size, sizeof(reply.val));
			break;
		case EC_FS_ARG_SIZE:
			if (fstat(fd, &st) == -1) {
				fs_errno(c);
				return;
			}
			fs_write_val(reply.val, st.st_blocks * S_BLKSIZE, sizeof(reply.val));
			break;
		default:
			fs_error(c, 0xff, "Bad argument to get_args");
			return;
		}
		reply.std_tx.command_code = EC_FS_CC_DONE;
		reply.std_tx.return_code = EC_FS_RC_OK;
		fs_reply(c, &(reply.std_tx), sizeof(reply));
	} else {
		fs_err(c, EC_FS_E_CHANNEL);
	}
}

void
fs_set_args(c)
	struct fs_context *c;
{
	struct ec_fs_reply reply;
	struct ec_fs_req_set_args *request;
	off_t val;
	int h, fd;

	if (c->client == NULL) {
		fs_err(c, EC_FS_E_WHOAREYOU);
		return;
	}
	request = (struct ec_fs_req_set_args *)(c->req);
	val = fs_read_val(request->val, sizeof(request->val));
	if (debug) printf("set args [%d, %d := %jx]\n", request->handle, request->arg, (uintmax_t)val);
	if ((h = fs_check_handle(c->client, request->handle)) != 0) {
		c->client->handles[h]->sequence ^= 1;
		fd = c->client->handles[h]->fd;
		switch (request->arg) {
		case EC_FS_ARG_PTR:
			if (lseek(fd, val, SEEK_SET) == -1) {
				fs_errno(c);
				return;
			}
		}
		reply.command_code = EC_FS_CC_DONE;
		reply.return_code = EC_FS_RC_OK;
		fs_reply(c, &reply, sizeof(reply));
	} else
		fs_err(c, EC_FS_E_CHANNEL);
}

void
fs_putbyte(c)
	struct fs_context *c;
{
	struct ec_fs_reply reply;
	struct ec_fs_req_putbyte *request;
	int h, fd;
	off_t off, saved_off = 0;
	size_t size, got;

	if (c->client == NULL) {
		fs_err(c, EC_FS_E_WHOAREYOU);
		return;
	}
	request = (struct ec_fs_req_putbyte *)(c->req);
	if (debug) printf("putbyte [%d, 0x%02x]\n", request->handle, request->byte);
	if ((h = fs_check_handle(c->client, request->handle)) != 0) {
		fd = c->client->handles[h]->fd;
		if (write(fd, &request->byte, 1) < 0) {
			fs_errno(c);
			return;
		}
		reply.command_code = EC_FS_CC_DONE;
		reply.return_code = EC_FS_RC_OK;
		fs_reply(c, &reply, sizeof(reply));
	}
	
}

void
fs_getbytes(c)
	struct fs_context *c;
{
	struct ec_fs_reply reply1;
	struct ec_fs_reply_getbytes2 reply2;
	struct ec_fs_req_getbytes *request;
	int h, fd;
	off_t off, saved_off = 0;
	size_t size, got;

	if (c->client == NULL) {
		fs_err(c, EC_FS_E_WHOAREYOU);
		return;
	}
	request = (struct ec_fs_req_getbytes *)(c->req);
	size = fs_read_val(request->nbytes, sizeof(request->nbytes));
	off = fs_read_val(request->offset, sizeof(request->offset));
	if (debug) printf("getbytes [%d, %zu%s%ju]\n", request->handle, size, request->use_ptr ? "!" : "@", (uintmax_t)off);
	if ((h = fs_check_handle(c->client, request->handle)) != 0) {
		fd = c->client->handles[h]->fd;
		if (!request->use_ptr)
			if ((saved_off = lseek(fd, 0, SEEK_CUR)) == -1 ||
			    lseek(fd, off, SEEK_SET) == -1) {
				fs_errno(c);
				return;
			}
		reply1.command_code = EC_FS_CC_DONE;
		reply1.return_code = EC_FS_RC_OK;
		fs_reply(c, &reply1, sizeof(reply1));
		reply2.std_tx.command_code = EC_FS_CC_DONE;
		reply2.std_tx.return_code = EC_FS_RC_OK;
		got = fs_data_send(c, fd, size);
		if (!request->use_ptr && lseek(fd, saved_off, SEEK_SET) == -1) {
			fs_errno(c);
			return;
		}
		if (got == -1) {
			/* Error */
			fs_errno(c);
		} else {
			if (got == size)
				reply2.flag = 0;
			else
				reply2.flag = 0x80; /* EOF reached */
			fs_write_val(reply2.nbytes, got, sizeof(reply2.nbytes));
			fs_reply(c, &(reply2.std_tx), sizeof(reply2));
		}
	}
	
}

void
fs_putbytes(c)
	struct fs_context *c;
{
	struct ec_fs_reply_putbytes1 reply1;
	struct ec_fs_reply_putbytes2 reply2;
	struct ec_fs_req_putbytes *request;
	int h, fd, replyport;
	off_t off, saved_off = 0;
	size_t size, got;

	if (c->client == NULL) {
		fs_err(c, EC_FS_E_WHOAREYOU);
		return;
	}
	replyport = c->req->reply_port;
	request = (struct ec_fs_req_putbytes *)(c->req);
	size = fs_read_val(request->nbytes, sizeof(request->nbytes));
	off = fs_read_val(request->offset, sizeof(request->offset));
	if (debug) printf("putbytes [%d, %zu%s%ju]\n", request->handle, size, request->use_ptr ? "!" : "@", (uintmax_t)off);
	if ((h = fs_check_handle(c->client, request->handle)) != 0) {
		fd = c->client->handles[h]->fd;
		if (!request->use_ptr)
			if ((saved_off = lseek(fd, 0, SEEK_CUR)) == -1 ||
			    lseek(fd, off, SEEK_SET) == -1) {
				fs_errno(c);
				return;
			}
		reply1.std_tx.command_code = EC_FS_CC_DONE;
		reply1.std_tx.return_code = EC_FS_RC_OK;
		reply1.data_port = OUR_DATA_PORT;
	        fs_write_val(reply1.block_size, aunfuncs->max_block,
			     sizeof(reply1.block_size));
		fs_reply(c, &(reply1.std_tx), sizeof(reply1));
		reply2.std_tx.command_code = EC_FS_CC_DONE;
		reply2.std_tx.return_code = EC_FS_RC_OK;
		got = fs_data_recv(c, fd, size, c->req->urd);
		if (!request->use_ptr && lseek(fd, saved_off, SEEK_SET) == -1) {
			fs_errno(c);
			return;
		}
		if (got == -1) {
			/* Error */
			fs_errno(c);
		} else {
			reply2.zero = 0;
			fs_write_val(reply2.nbytes, got, sizeof(reply2.nbytes));
			c->req->reply_port = replyport;
			fs_reply(c, &(reply2.std_tx), sizeof(reply2));
		}
	}
}

void
fs_load(c)
	struct fs_context *c;
{
	struct ec_fs_reply_load1 reply1;
	struct ec_fs_reply_load2 reply2;
	struct ec_fs_req_load *request;
	char *upath, *path_argv[2];
	int fd;
	size_t size, got;
	FTS *ftsp;
	FTSENT *f;

	if (c->client == NULL) {
		fs_err(c, EC_FS_E_WHOAREYOU);
		return;
	}
	request = (struct ec_fs_req_load *)(c->req);
	request->path[strcspn(request->path, "\r")] = '\0';
	if (debug) printf("load [%s]\n", request->path);
	upath = fs_unixify_path(c, request->path);
	if (upath == NULL) {
		fs_err(c, EC_FS_E_NOMEM);
		return;
	}
	if ((fd = open(upath, O_RDONLY)) == -1) {
		fs_errno(c);
		free(upath);
		return;
	}
	path_argv[0] = upath;
	path_argv[1] = NULL;
	ftsp = fts_open(path_argv, FTS_LOGICAL, NULL);
	f = fts_read(ftsp);
	fs_get_meta(f, &(reply1.meta));
	fs_write_val(reply1.size, f->fts_statp->st_size, sizeof(reply1.size));
	reply1.access = fs_mode_to_access(f->fts_statp->st_mode);
	fs_write_date(&(reply1.date), f->fts_statp->st_ctime);
	reply1.std_tx.command_code = EC_FS_CC_DONE;
	reply1.std_tx.return_code = EC_FS_RC_OK;
	fs_reply(c, &(reply1.std_tx), sizeof(reply1));
	reply2.std_tx.command_code = EC_FS_CC_DONE;
	reply2.std_tx.return_code = EC_FS_RC_OK;
	got = fs_data_send(c, fd, f->fts_statp->st_size);
	if (got == -1) {
		/* Error */
		fs_errno(c);
	} else {
		fs_reply(c, &(reply2.std_tx), sizeof(reply2));
	}
	close(fd);
	fts_close(ftsp);
}

void
fs_save(c)
	struct fs_context *c;
{
	struct ec_fs_reply_save1 reply1;
	struct ec_fs_reply_save2 reply2;
	struct ec_fs_req_save *request;
	struct ec_fs_meta meta;
	char *upath, *path_argv[2];
	int fd, ackport, replyport;
	size_t size, got;
	FTS *ftsp;
	FTSENT *f;

	if (c->client == NULL) {
		fs_err(c, EC_FS_E_WHOAREYOU);
		return;
	}
	request = (struct ec_fs_req_save *)(c->req);
	request->path[strcspn(request->path, "\r")] = '\0';
	replyport = c->req->reply_port;
	ackport = c->req->urd;
	if (debug) printf("save [%s]\n", request->path);
	size = fs_read_val(request->size, sizeof(request->size));
	upath = fs_unixify_path(c, request->path);
	if (upath == NULL) {
		fs_err(c, EC_FS_E_NOMEM);
		return;
	}
	if ((fd = open(upath, O_CREAT|O_TRUNC|O_RDWR, 0666)) == -1) {
		fs_errno(c);
		free(upath);
		return;
	}
	meta = request->meta;
	reply1.std_tx.command_code = EC_FS_CC_DONE;
	reply1.std_tx.return_code = EC_FS_RC_OK;
	reply1.data_port = OUR_DATA_PORT;
	fs_write_val(reply1.block_size, aunfuncs->max_block,
		     sizeof(reply1.block_size));
	fs_reply(c, &(reply1.std_tx), sizeof(reply1));
	reply2.std_tx.command_code = EC_FS_CC_DONE;
	reply2.std_tx.return_code = EC_FS_RC_OK;
	got = fs_data_recv(c, fd, size, ackport);
	close(fd);
	if (got == -1) {
		/* Error */
		fs_errno(c);
	} else {
		/*
		 * Write load and execute addresses from the
		 * request, and return the file date in the
		 * response.
		 */
		path_argv[0] = upath;
		path_argv[1] = NULL;
		ftsp = fts_open(path_argv, FTS_LOGICAL, NULL);
		f = fts_read(ftsp);
		fs_set_meta(f, &meta);
		fs_write_date(&(reply2.date), f->fts_statp->st_ctime);
		reply2.access = fs_mode_to_access(f->fts_statp->st_mode);
		fts_close(ftsp);
		c->req->reply_port = replyport;
		fs_reply(c, &(reply2.std_tx), sizeof(reply2));
	}
}

static ssize_t
fs_data_send(c, fd, size)
	struct fs_context *c;
	int fd;
	size_t size;
{
	struct aun_packet *pkt;
	void *buf;
	ssize_t result;
	size_t this, done;
	int faking;

	if ((pkt = malloc(sizeof(*pkt) + (size > aunfuncs->max_block ? aunfuncs->max_block : size))) == NULL) { 
		fs_err(c, EC_FS_E_NOMEM);
		return -1;
	}
	buf = pkt->data;
	faking = 0;
	done = 0;
	while (size) {
		this = size > aunfuncs->max_block ? aunfuncs->max_block : size;
		if (!faking) {
			result = read(fd, buf, this);
			if (result > 0) {
				/* Normal -- the kernel had something for us */
				this = result;
				done += this;
			} else { /* EOF or error */
				if (result == -1) done = result;
				faking = 1;
			}
		}
		pkt->type = AUN_TYPE_UNICAST;
		pkt->dest_port = c->req->urd;
		pkt->flag = c->req->aun.flag & 1;
		if (aunfuncs->xmit(pkt, sizeof(*pkt) + this, c->from) == -1)
			warn("send data");
		size -= this;
	}
	free(pkt);
	return done;
}

static ssize_t
fs_data_recv(c, fd, size, ackport)
	struct fs_context *c;
	int fd;
	size_t size;
	int ackport;
{
	struct aun_packet *pkt, *ack;
	void *buf;
	ssize_t msgsize, result;
	struct aun_srcaddr from;
	size_t this, done;

	if ((ack = malloc(sizeof(*ack) + 1)) == NULL) {
		fs_err(c, EC_FS_E_NOMEM);
		return -1;
	}
	done = 0;
	while (size) {
		pkt = aunfuncs->recv(&msgsize, &from);
		msgsize -= sizeof(struct aun_packet);
		if (pkt->dest_port != OUR_DATA_PORT ||
		    memcmp(&from, c->from, sizeof(from))) {
			fs_error(c, 0xFF, "I'm confused");
			return -1;
		}
		result = write(fd, pkt->data, msgsize);
		if (result < 0) {
			fs_errno(c);
			return -1;
		}
		size -= msgsize;
		if (size) {
			/*
			 * Send partial ACK.
			 */
			ack->type = AUN_TYPE_UNICAST;
			ack->dest_port = ackport;
			ack->flag = 0;
			ack->data[0] = 0;
			if (aunfuncs->xmit(ack, sizeof(*ack) + 1, c->from) == -1)
				warn("send data");
		}
	}
	free(ack);
	return done;
}
