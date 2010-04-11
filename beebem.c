/*-
 * Copyright (c) 2010 Simon Tatham
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
 * Implementation of Unix BeebEm's ad-hoc UDP Econet encapsulation,
 * for aund.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/select.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <ctype.h>
#include <err.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <fcntl.h>

#include "aun.h"
#include "extern.h"
#include "fileserver.h"
#include "version.h"

struct econet_addr {
	uint8_t station;
	uint8_t network;
};

static int sock;
static unsigned char sbuf[65536];
static unsigned char rbuf[65536];
static struct aun_packet *const rpkt = (struct aun_packet *)rbuf;

/* Offset of packet payload in struct aun_packet:
 * size of AUN header minus size of Econet header. */
#define PKTOFF (offsetof(struct aun_packet, data) - 4)

union internal_addr {
	struct aun_srcaddr srcaddr;
	struct econet_addr eaddr;
};

/* Map Econet addresses to (ip,port) */
static struct ipport {
	struct in_addr addr;
	int port;
} ec2ip[256*256];		       /* index is network*256+station */

/* List of Econet addresses (as network*256+station) actually in use */
static unsigned short eclist[256*256];
static int eccount = 0;

/* (FIXME: probably should make this configurable) */
static int our_econet_addr = 254;      /* 0.254 */

static void
beebem_setup(void)
{
	struct sockaddr_in name;
	FILE *fp;
	char linebuf[512];
	int lineno;
	int fl;

	/*
	 * Start by reading the BeebEm Econet configuration file.
	 */
	fp = fopen(beebem_cfg_file, "r");
	if (!fp)
		err(1, "open %s", beebem_cfg_file);
	lineno = 0;
	while (lineno++, fgets(linebuf, sizeof(linebuf), fp)) {
		int network, station, ecaddr;
		struct in_addr addr;
		int port;
		char *p = linebuf, *q;

		while (*p && isspace((unsigned char)*p)) p++;
		if (*p == '#' || !*p)
			continue;

		network = atoi(p);
		while (*p && !isspace((unsigned char)*p)) p++;
		while (*p && isspace((unsigned char)*p)) p++;

		station = atoi(p);
		while (*p && !isspace((unsigned char)*p)) p++;
		while (*p && isspace((unsigned char)*p)) p++;

		q = p;
		while (*p && !isspace((unsigned char)*p)) p++;
		if (*p) *p++ = '\0';
		while (*p && isspace((unsigned char)*p)) p++;
		addr.s_addr = inet_addr(q);

		port = atoi(p);

		if (!port)
			errx(1, "%s:%d: malformed config line",
			     beebem_cfg_file, lineno);

		ecaddr = network * 256 + station;
		if (ec2ip[ecaddr].port)
			errx(1, "%s:%d: Econet station %d.%d listed twice",
			     beebem_cfg_file, lineno, network, station);
		ec2ip[ecaddr].addr = addr;
		ec2ip[ecaddr].port = port;
		eclist[eccount++] = ecaddr;
	}
	fclose(fp);

	/*
	 * Make sure the config file listed details for the Econet
	 * address we actually want.
	 */
	if (!ec2ip[our_econet_addr].port)
		errx(1, "fileserver address %d.%d not listed in %s",
		     our_econet_addr >> 8, our_econet_addr & 0xFF,
		     beebem_cfg_file);

	/*
	 * Now set up our UDP socket.
	 */
	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0)
		err(1, "socket");
	name.sin_family = AF_INET;
	name.sin_addr = ec2ip[our_econet_addr].addr;
	name.sin_port = htons(ec2ip[our_econet_addr].port);
	if (bind(sock, (struct sockaddr*)&name, sizeof(name)))
		err(1, "bind");
	if ((fl = fcntl(sock, F_GETFL)) < 0)
		err(1, "fcntl(F_GETFL)");
        if (fcntl(sock, F_SETFL, fl | O_NONBLOCK) < 0)
		err(1, "fcntl(F_SETFL)");
}

static ssize_t beebem_listen(unsigned *addr, int forever)
{
	ssize_t msgsize;
	struct sockaddr_in from;
	unsigned their_addr;
	fd_set r;
	struct timeval timeout;

	while (1) {
		socklen_t fromlen = sizeof(from);
		int i;

		/*
		 * We set the socket to nonblocking mode, and must
		 * therefore always select before we recvfrom. The
		 * timeout varies depending on 'forever'.
		 */
		FD_ZERO(&r);
		FD_SET(sock, &r);
		timeout.tv_sec = 0;
		timeout.tv_usec = 100000;   /* 100ms */
		i = select(sock+1, &r, NULL, NULL, forever ? NULL : &timeout);
		if (i == 0)
			return 0;      /* nothing turned up */

		msgsize = recvfrom(sock, rbuf + PKTOFF,
				   sizeof(rbuf) - PKTOFF,
				   0, (struct sockaddr *)&from, &fromlen);
		if (msgsize == -1)
			err(1, "recvfrom");

		if (msgsize < 4)
			continue;      /* not big enough for an Econet frame */

		/* Is it for us? */
		if (256 * rbuf[PKTOFF+1] + rbuf[PKTOFF] != our_econet_addr)
			continue;

		/* Who's it from? */
		their_addr = 256 * rbuf[PKTOFF+3] + rbuf[PKTOFF+2];

		/* Is it _really_ from them? */
		if (!ec2ip[their_addr].port ||
		    from.sin_addr.s_addr != ec2ip[their_addr].addr.s_addr ||
		    ntohs(from.sin_port) != ec2ip[their_addr].port) {
			if (debug)
				printf("failed ingress filter from %s:%d (claimed to be %d.%d)\n",
				       inet_ntoa(from.sin_addr), ntohs(from.sin_port),
				       rbuf[PKTOFF+1], rbuf[PKTOFF]);
			continue;
		}

		*addr = their_addr;
		return msgsize;
	}
}

static void beebem_send(const void *data, ssize_t len)
{
	int i;
	struct sockaddr_in to;

	/*
	 * We're emulating a broadcast medium, so we should attempt
	 * to send to all stations (except ourself!) regardless of
	 * the target address in the header. (A useful consequence
	 * of doing this is that I could run SJmon on a spare BeebEm
	 * to debug filesystem traffic...)
	 */
	for (i = 0; i < eccount; i++) {
		unsigned ecaddr = eclist[i];
		if (ecaddr == our_econet_addr)
			continue;
		to.sin_family = AF_INET;
		to.sin_addr = ec2ip[ecaddr].addr;
		to.sin_port = htons(ec2ip[ecaddr].port);
		if (sendto(sock, data, len, 0,
			   (struct sockaddr*)&to, sizeof(to)) < 0)
			err(1, "sendto");
	}
}

static struct aun_packet *
beebem_recv(ssize_t *outsize, struct aun_srcaddr *vfrom)
{
	ssize_t msgsize;
	union internal_addr *afrom = (union internal_addr *)vfrom;
	int scoutaddr, mainaddr;
	int ctlbyte, destport;
	unsigned char ack[8];

	while (1) {
		/*
		 * Listen for a scout packet. This should be 6 bytes
		 * long, and the second payload byte should indicate
		 * the destination port.
		 */
		msgsize = beebem_listen(&scoutaddr, 1);

		ack[0] = scoutaddr & 0xFF;
		ack[1] = scoutaddr >> 8;
		ack[2] = our_econet_addr & 0xFF;
		ack[3] = our_econet_addr >> 8;

		if (rbuf[PKTOFF+5] == 0) {
			/*
			 * Port 0 means an immediate operation. We
			 * only support Machine Type Peek.
			 */
			if (rbuf[PKTOFF+4] == 0x88) {
				ack[4] = AUND_MACHINE_PEEK_LO;
				ack[5] = AUND_MACHINE_PEEK_HI;
				ack[6] = AUND_VERSION_MINOR;
				ack[7] = AUND_VERSION_MAJOR;
			}
			beebem_send(ack, 8);
			continue;
		}
		if (msgsize != 6) {
			if (debug)
				printf("received wrong-size scout packet (%zd) from %d.%d\n",
				       msgsize, scoutaddr>>8, scoutaddr&0xFF);
			continue;
		}

		ctlbyte = rbuf[PKTOFF+4];
		destport = rbuf[PKTOFF+5];

		/*
		 * Send an ACK, repeatedly if necessary, and wait
		 * for the main packet, which should come
		 * from the same address.
		 *
		 * (This is painfully single-threaded, but I'm
		 * currently working on the assumption that it's an
		 * accurate reflection of the way a real Econet
		 * four-way handshake would tie up the bus for all
		 * other stations until it had finished.)
		 */
		do
			beebem_send(ack, 4);
		while ((msgsize = beebem_listen(&mainaddr, 0)) == 0);
		if (mainaddr != scoutaddr) {
			if (debug)
				printf("expected payload packet from %d.%d,"
				       " received something from %d.%d instead\n",
				       scoutaddr>>8, scoutaddr&0xFF,
				       mainaddr>>8, mainaddr&0xFF);
			continue;
		}

		/*
		 * ACK that too. (We can reuse the ACK we
		 * constructed above.)
		 */
		beebem_send(ack, 4);

		/*
		 * Now fake up an aun_packet structure to return.
		 */
		rpkt->type = AUN_TYPE_UNICAST;   /* shouldn't matter */
		rpkt->dest_port = destport; 
		rpkt->flag = ctlbyte;
		rpkt->retrans = 0;
		memset(rpkt->seq, 0, 4);
		*outsize = msgsize + PKTOFF;
		memset(afrom, 0, sizeof(struct aun_srcaddr));
		afrom->eaddr.network = scoutaddr >> 8;
		afrom->eaddr.station = scoutaddr & 0xFF;
		return rpkt;
	}
}

static ssize_t
beebem_xmit(struct aun_packet *spkt, size_t len, struct aun_srcaddr *vto)
{
	union internal_addr *ato = (union internal_addr *)vto;
	int theiraddr, ackaddr;
	ssize_t msgsize, payloadlen;

	if (len > sizeof(sbuf) - 4) {
		if (debug)
			printf("outgoing packet too large (%zu)\n", len);
		return -1;
	}

	/*
	 * Send the scout packet, and wait for an ACK.
	 */
	sbuf[0] = ato->eaddr.station;
	sbuf[1] = ato->eaddr.network;
	sbuf[2] = our_econet_addr & 0xFF;
	sbuf[3] = our_econet_addr >> 8;
	sbuf[4] = 0x80 | spkt->flag;
	sbuf[5] = spkt->dest_port;
	do
		beebem_send(sbuf, 6);
	while ((msgsize = beebem_listen(&ackaddr, 0)) == 0);

	/*
	 * We expect the ACK to have come from the right address.
	 * (See 'painfully single-threaded' caveat above.)
	 */
	theiraddr = ato->eaddr.network * 256 + ato->eaddr.station;
	if (ackaddr != theiraddr) {
		if (debug)
			printf("expected ack packet from %d.%d,"
			       " received something from %d.%d instead\n",
			       theiraddr>>8, theiraddr&0xFF,
			       ackaddr>>8, ackaddr&0xFF);
		return -1;
	}
	if (msgsize != 4) {
		if (debug)
			printf("received wrong-size ack packet (%zd) from %d.%d\n",
			       msgsize, theiraddr>>8, theiraddr&0xFF);
		return -1;
	}

	/*
	 * Construct and send the payload packet, and wait for an
	 * ACK.
	 */
	sbuf[0] = ato->eaddr.station;
	sbuf[1] = ato->eaddr.network;
	sbuf[2] = our_econet_addr & 0xFF;
	sbuf[3] = our_econet_addr >> 8;
	payloadlen = len - offsetof(struct aun_packet, data);
	memcpy(sbuf + 4, spkt->data, payloadlen);
	do
		beebem_send(sbuf, payloadlen+4);
	while ((msgsize = beebem_listen(&ackaddr, 0)) == 0);

	/*
	 * The second ACK, just as above, should have come from the
	 * right address.
	 */
	theiraddr = ato->eaddr.network * 256 + ato->eaddr.station;
	if (ackaddr != theiraddr) {
		if (debug)
			printf("expected ack packet from %d.%d,"
			       " received something from %d.%d instead\n",
			       theiraddr>>8, theiraddr&0xFF,
			       ackaddr>>8, ackaddr&0xFF);
		return -1;
	}
	if (msgsize != 4) {
		if (debug)
			printf("received wrong-size ack packet (%zd) from %d.%d\n",
			       msgsize, theiraddr>>8, theiraddr&0xFF);
		return -1;
	}

	return len;
}

static char *
beebem_ntoa(struct aun_srcaddr *vfrom)
{
	union internal_addr *afrom = (union internal_addr *)vfrom;
	static char retbuf[80];
	sprintf(retbuf, "station %d.%d", afrom->eaddr.network,
		afrom->eaddr.station);
	return retbuf;
}

static void
beebem_get_stn(struct aun_srcaddr *vfrom, uint8_t *out)
{
	union internal_addr *afrom = (union internal_addr *)vfrom;
	out[0] = afrom->eaddr.station;
	out[1] = afrom->eaddr.network;
}

const struct aun_funcs beebem = {
	512,
	beebem_setup,
	beebem_recv,
        beebem_xmit,
        beebem_ntoa,
        beebem_get_stn,
};
