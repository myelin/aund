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

#ifndef _AUN_H
#define _AUN_H

#include <sys/types.h>

#define PORT_AUN 32768

struct aun_packet {
	u_int8_t type;
#define AUN_TYPE_BROADCAST	1
#define AUN_TYPE_UNICAST	2
#define AUN_TYPE_ACK		3
/* NACK of some sort?		4 */
#define AUN_TYPE_IMMEDIATE	5
#define AUN_TYPE_IMM_REPLY	6
	u_int8_t dest_port;
	u_int8_t flag;
	u_int8_t retrans;
	u_int8_t seq[4]; /* little-endian */
	u_int8_t data[0]; /* actually more */
};

#define AUN_MAX_BLOCK 4096

#define EC_PORT_FS 0x99
#define EC_PORT_PS_STATUS_ENQ 0x9f
#define EC_PORT_PS_STATUS_REPLY 0x9e
#define EC_PORT_PS_JOB	0xd1

#endif
