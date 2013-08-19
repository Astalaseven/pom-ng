/*
 *  This file is part of pom-ng.
 *  Copyright (C) 2012-2013 Guy Martin <gmsoft@tuxicoman.be>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef __PROTO_TFTP_H__
#define __PROTO_TFTP_H__

#define PROTO_TFTP_FIELD_NUM	1
#define PROTO_TFTP_EXPT_TIMER	30
#define PROTO_TFTP_PKT_TIMER 	60
#define PROTO_TFTP_BLK_SIZE		512
#define PROTO_TFTP_STREAM_BUFF	16 * PROTO_TFTP_BLK_SIZE

#define PROTO_TFTP_CONN_INVALID 0x1

struct proto_tftp_conntrack_priv {

	int flags;
	struct stream *stream;

};

struct mod_reg_info* proto_tftp_reg_info();
static int proto_tftp_mod_register(struct mod_reg *mod);
static int proto_tftp_init(struct proto *proto, struct registry_instance *i);
static int proto_tftp_process(void *proto_priv, struct packet *p, struct proto_process_stack *stack, unsigned int stack_index);
static int proto_tftp_mod_unregister();
static int proto_tftp_conntrack_cleanup(void *ce_priv);
static int proto_tftp_process_payload(struct conntrack_entry *ce, struct packet *p, struct proto_process_stack *stack, unsigned int stack_index);

#endif
