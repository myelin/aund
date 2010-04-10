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
#include <sys/time.h>

#include <err.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "aun.h"
#include "extern.h"
#include "fileserver.h"

#define MACHINE_MAKE 0
#define MACHINE_MODEL 0x0e
#define ECONET_SW_VERSION_MAJOR 0
#define ECONET_SW_VERSION_MINOR 1

#define EC_PORT_FS 0x99

extern const struct aun_funcs aun, beebem;

int debug = 1;
int using_syslog = 1;
char *beebem_cfg_file = NULL;
const struct aun_funcs *aunfuncs = &aun;

volatile int painful_death = 0;

int main __P((int, char*[]));

static void sig_init __P((void));
static void sigcatcher __P((int));

int
main(argc, argv)
	int argc;
	char *argv[];
{
	const char *conffile = "/etc/aund.conf";
	int c;
	int override_debug = -1;
	int override_syslog = -1;

	while ((c = getopt(argc, argv, "f:dDsS")) != -1) {
		switch (c) {
		    case '?':
			return 1;      /* getopt parsing error */
		    case 'f':
			conffile = optarg;
			break;
		    case 'd':
			override_debug = 1;
			break;
		    case 'D':
			override_debug = 0;
			break;
		    case 's':
			override_syslog = 1;
			break;
		    case 'S':
			override_syslog = 0;
			break;
		}
	}

	sig_init();
	fs_init();
	conf_init(conffile);

	if (!fixedurd && !pwfile)
		errx(1, "must specify either 'urd' or 'pwfile' in configuration");

	if (beebem_cfg_file)
		aunfuncs = &beebem;

	/*
	 * Override specifications from the configuration file with
	 * those from the command line.
	 */
	if (override_debug != -1)
		debug = override_debug;
	if (override_syslog != -1)
		using_syslog = override_syslog;

	if (debug) setlinebuf(stdout);

	aunfuncs->setup();

	if (!debug)
		if (daemon(0, 0) != 0)
			err(1, "daemon");
	if (using_syslog) {
		openlog("aund", LOG_PID | (debug ? LOG_PERROR : 0),
			LOG_DAEMON);
		syslog(LOG_NOTICE, "started");
	}

	/*
	 * We'll use relative pathnames for all our file accesses,
	 * so start by setting our cwd to the root of the fs we're
	 * serving.
	 */
	chdir(root);

	for (;!painful_death;) {
		ssize_t msgsize;
		struct aun_packet *pkt;
		struct aun_srcaddr from;

		pkt = aunfuncs->recv(&msgsize, &from);

		switch (pkt->dest_port) {
		    case EC_PORT_FS:
			if (debug) printf("\n\t(file server: ");
			file_server(pkt, msgsize, &from);
			if (debug) printf(")");
			break;
		}
		if (debug) printf("\n");
	}
	return 0;
}

static void
sig_init()
{
	struct sigaction sa;

	sa.sa_handler = sigcatcher;
	sigemptyset(&(sa.sa_mask));
	sa.sa_flags = 0;
	sigaction(SIGINT, &sa, NULL);
}

static void
sigcatcher(s)
	int s;
{
	painful_death = 1;
}
