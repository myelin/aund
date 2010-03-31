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
#include <sys/time.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <err.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "aun.h"
#include "extern.h"

#define MACHINE_MAKE 0
#define MACHINE_MODEL 0x0e
#define ECONET_SW_VERSION_MAJOR 0
#define ECONET_SW_VERSION_MINOR 1

#define EC_PORT_FS 0x99

static void aun_ack(int sock, struct aun_packet *pkt, struct sockaddr_in *from);

int sock;
unsigned char buf[65536];

union internal_addr {
	struct aun_srcaddr srcaddr;
	struct in_addr sin_addr;
};

static void
aun_setup(void)
{
	struct sockaddr_in name;

	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0)
		err(1, "socket");
	name.sin_family = AF_INET;
	name.sin_addr.s_addr = INADDR_ANY;
	name.sin_port = htons(PORT_AUN);
	if (bind(sock, (struct sockaddr*)&name, sizeof(name)))
		err(1, "bind");
}

static struct aun_packet *
aun_recv(ssize_t *outsize, struct aun_srcaddr *vfrom)
{
	ssize_t msgsize;
	struct aun_packet *pkt = (struct aun_packet *)buf;
	union internal_addr *afrom = (union internal_addr *)vfrom;
	struct sockaddr_in from;

	while (1) {
		socklen_t fromlen = sizeof(from);
		int i;
		msgsize = recvfrom(sock, pkt, sizeof(buf), 0, (struct sockaddr *)&from, &fromlen);
		if (msgsize == -1)
			err(1, "recvfrom");
		if (0) {
			printf("Rx");
			for (i = 0; i < msgsize; i++) {
				printf(" %02x", buf[i]);
			}
			printf(" from UDP port %hu", ntohs(from.sin_port));
		}
		/* Replies seem always to go to port 32768 */
		from.sin_port = htons(PORT_AUN);
		switch (pkt->type) {
		case AUN_TYPE_IMMEDIATE:
			if (pkt->flag == 8) {
				/* Echo request? */
				pkt->type = AUN_TYPE_IMM_REPLY;
				pkt->data[0] = MACHINE_MODEL;
				pkt->data[1] = MACHINE_MAKE;
				pkt->data[2] = ECONET_SW_VERSION_MINOR;
				pkt->data[3] = ECONET_SW_VERSION_MAJOR;
				if (sendto(sock, buf, 12, 0,
					   (struct sockaddr*)&from,
					   sizeof(from))
				    == -1) {
					err(1, "sendto(echo reply)");
				}
				if (debug) printf(" (echo request)");
			}
			break;
		case AUN_TYPE_UNICAST:
			aun_ack(sock, pkt, &from);
			/* FALLTHROUGH */
		case AUN_TYPE_BROADCAST:
			/* Real packet; return it. */
			*outsize = msgsize;
			afrom->sin_addr = from.sin_addr;
			return pkt;
		}
	}
}

static void
aun_ack(sock, pkt, from)
	int sock;
	struct aun_packet *pkt;
	struct sockaddr_in *from;
{
	struct aun_packet ack; /* No data */
	int i;
	ack.type = AUN_TYPE_ACK;
	ack.dest_port = 0;
	ack.flag = 0;
	ack.retrans = 0;
	for (i=0;i<4;i++) ack.seq[i] = pkt->seq[i];
	if (sendto(sock, &ack, sizeof(ack), 0, (struct sockaddr *)from, sizeof(*from)) == -1) {
		err(1, "sendto (ack)");
	}
}

static ssize_t
aun_xmit(pkt, len, vto)
	struct aun_packet *pkt;
	size_t len;
	struct aun_srcaddr *vto;
{
	static u_int32_t sequence = 2;
	struct aun_packet buf;
	union internal_addr *ato = (union internal_addr *)vto;
	struct sockaddr_in from, to;
	socklen_t fromlen;
	int i;
	ssize_t retval;

	fromlen = sizeof(from);
	pkt->retrans = 0;
	pkt->seq[0] = (sequence & 0x000000ff);
	pkt->seq[1] = (sequence & 0x0000ff00) >> 8;
	pkt->seq[2] = (sequence & 0x00ff0000) >> 16;
	pkt->seq[3] = (sequence & 0xff000000) >> 24;
	sequence += 4;
	to.sin_family = AF_INET;
	to.sin_addr = ato->sin_addr;
	to.sin_port = htons(PORT_AUN);
	if (0) {
		printf("Tx");
		for (i = 0; i < len; i++) {
			printf(" %02x", ((unsigned char *)pkt)[i]);
		}
		printf(" to UDP port %hu\n", ntohs(to.sin_port));
	}
	for(;;) {
		retval = sendto(sock, pkt, len, 0, (struct sockaddr *)&to, sizeof(to));
		/* Grotty hack to see if it works */
		if (retval < 0) return retval;
		if (pkt->type == AUN_TYPE_UNICAST) {
			int nready;
			fd_set fdset;
			struct timeval timeout;

			timeout.tv_sec = 0;
			timeout.tv_usec = 100000;
			FD_ZERO(&fdset);
			FD_SET(sock, &fdset);
			do {
				nready = select(FD_SETSIZE, &fdset, NULL, NULL, &timeout);
				if (FD_ISSET(sock, &fdset)) {
					recvfrom(sock, &buf, 64, 0, (struct sockaddr *)&from, &fromlen);
					/* Is this an ack of the right packet? */
					if (from.sin_addr.s_addr == to.sin_addr.s_addr &&
					    buf.type == AUN_TYPE_ACK &&
					    memcmp(&(buf.seq), &(pkt->seq), 4) == 0)
						return retval;
				}
			} while (nready > 0);
			/* Timeout.  Retransmit. */
		} else
			return retval;
	}
	/* NOTREACHED */
}

static char *
aun_ntoa(struct aun_srcaddr *vfrom)
{
	union internal_addr *afrom = (union internal_addr *)vfrom;
	return inet_ntoa(afrom->sin_addr);
}

const struct aun_funcs aun = {
	aun_setup,
	aun_recv,
        aun_xmit,
        aun_ntoa,
};
