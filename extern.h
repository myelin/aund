/* $NetBSD: extern.h,v 1.1 2001/02/06 23:54:46 bjh21 Exp $ */
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


#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "aun.h"

/*
 * Opaque structure holding a source address.
 */
struct aun_srcaddr {
	u_int8_t bytes[8];
};

extern void print_status(struct aun_packet *, ssize_t, struct aun_srcaddr *);
extern void print_job(struct aun_packet *, ssize_t, struct aun_srcaddr *);
extern void conf_init __P((const char *));
extern void fs_init __P((void));
extern void file_server __P((struct aun_packet *, ssize_t, struct aun_srcaddr *));

extern char *pw_validate(char *user, const char *pw);
extern int pw_change(const char *user, const char *oldpw, const char *newpw);

extern int debug;
extern int using_syslog;
extern char *beebem_cfg_file;

struct aun_funcs {
	void (*setup)(void);
	struct aun_packet *(*recv)(ssize_t *outsize,
				   struct aun_srcaddr *from);
	ssize_t (*xmit)(struct aun_packet *pkt,
			size_t len, struct aun_srcaddr *to);
	char *(*ntoa)(struct aun_srcaddr *addr);
};

extern const struct aun_funcs *aunfuncs;
