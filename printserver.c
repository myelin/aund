/* $NetBSD: printserver.c,v 1.1 2001/02/06 23:54:46 bjh21 Exp $ */
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
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include "aun.h"
#include "printserver.h"
void
print_status(pkt, len, from)
	struct aun_packet *pkt;
	ssize_t len;
	struct sockaddr_in *from;
{
	struct ec_ps_status_enq *question = (struct ec_ps_status_enq *)pkt->data;
	printf("name %6s, reason %d", question->name, question->reason);
	if (strncasecmp(question->name, "SPOOL ", 6) == 0 ||
	    strncasecmp(question->name, "PRINT ", 6) == 0) {
		/* Default printer */
		/* FIXME */
	}
	switch (question->reason) {
	case EC_PS_STATUS_ENQ_STATUS:
		printf(" (status request)");
		/* FIXME respond */
		break;
	case EC_PS_STATUS_ENQ_NAME:
		printf(" (name request)");
		/* FIXME respond */
		break;
	}
	/* There's no way to report errors, so just ignore oddness */
}

void
print_job(pkt, len, from)
	struct aun_packet *pkt;
	ssize_t len;
	struct sockaddr_in *from;
{
	/* XXX Where do I get the flag byte? */
	unsigned char flag;
	switch (flag && EC_PS_FLAG_MODE_MASK) {
	case EC_PS_FLAG_MODE_OPEN:
		/* A client wants to send a job. */
		/* XXX How do we know what printer to send this to? */
		if (flag & EC_PS_FLAG_TASKID_MASK == EC_PS_FLAG_TASKID_NEW) {
			/* Client wants a task ID assigned */
			/* XXX ... but I can't be bothered */
			flag = flag & ~EC_PS_FLAG_MODE_MASK | EC_PS_FLAG_MODE_LBLK;
		} else {
			/* They didn't ask for a taskid or long blocks */
			flag = flag & ~EC_PS_FLAG_MODE_MASK | EC_PS_FLAG_MODE_SBLK;
		}
		/* FIXME send reply */
		/* FIXME other modes */
	}
}

		
