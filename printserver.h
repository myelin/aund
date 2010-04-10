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
#ifndef _PRINTSERVER_H
#define _PRINTSERVER_H

/* Status Enquiry */

struct ec_ps_status_enq {
	char name[6]; /* Space padded -- PRINT and SPOOL are defaults */
	unsigned char reason;
#define EC_PS_STATUS_ENQ_STATUS	1
#define EC_PS_STATUS_ENQ_NAME	6
	unsigned char zero;
};

/* Status reply */

struct ec_ps_status_reply {
	char status;
#define EC_PS_STATUS_READY	0
#define EC_PS_STATUS_BUSY	1
#define EC_PS_STATUS_JAMMED	2
#define EC_PS_STATUS_OFFLINE	6
	char busy_station;
	char busy_net;
};

/* Name reply */

struct ec_ps_name_reply {
	char name[6]; /* Space padded */
};

/* Flag byte in job */

#define EC_PS_FLAG_SEQ		0x01
#define EC_PS_FLAG_MODE_MASK	0x06
#define EC_PS_FLAG_MODE_OPEN	0x00 /* Clients requests connection */
#define EC_PS_FLAG_MODE_LBLK	0x04 /* job in progress: Server will accept large blocks */
#define EC_PS_FLAG_MODE_SBLK	0x02 /* job in progress: Server will not accept large blocks */
#define EC_PS_FLAG_MODE_CLOSE	0x06 /* Client has finished transmitting */
#define EC_PS_FLAG_TASKID_MASK	0x74
#define EC_PS_FLAG_TASKID_NEW	0x40 /* large blocks, allocate a taskid */
#define EC_PS_EOT		0x03 /* last byte in job should be this */

#endif
