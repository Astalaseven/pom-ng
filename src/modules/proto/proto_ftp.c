/*
 *  This file is part of pom-ng.
 *  Copyright (C) 2013 Guy Martin <gmsoft@tuxicoman.be>
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

#include <pom-ng/ptype.h>
#include <pom-ng/proto.h>
#include <pom-ng/event.h>

#include "proto_ftp.h"
#include <pom-ng/ptype_string.h>
#include <pom-ng/ptype_uint16.h>

#include <string.h>
#include <stdio.h>


struct mod_reg_info* proto_ftp_reg_info() {

	static struct mod_reg_info reg_info = { 0 };
	reg_info.api_ver = MOD_API_VER;
	reg_info.register_func = proto_ftp_mod_register;
	reg_info.unregister_func = proto_ftp_mod_unregister;
	reg_info.dependencies = "ptype_string, ptype_uint16";

	return &reg_info;
}

static int proto_ftp_mod_register(struct mod_reg *mod) {

	static struct proto_reg_info proto_ftp = { 0 };
	proto_ftp.name = "ftp";
	proto_ftp.api_ver = PROTO_API_VER;
	proto_ftp.mod = mod;

	static struct conntrack_info ct_info = { 0 };

	ct_info.default_table_size = 1; // No hashing done here
	ct_info.cleanup_handler = proto_ftp_conntrack_cleanup;
	proto_ftp.ct_info = &ct_info;

	proto_ftp.init = proto_ftp_init;
	proto_ftp.process = proto_ftp_process;
	proto_ftp.post_process = proto_ftp_post_process;
	proto_ftp.cleanup = proto_ftp_cleanup;

	if (proto_register(&proto_ftp) == POM_OK)
		return POM_OK;

	return POM_ERR;

}

static int proto_ftp_init(struct proto *proto, struct registry_instance *i) {

	struct proto_ftp_priv *priv = malloc(sizeof(struct proto_ftp_priv));
	if (!priv) {
		pom_oom(sizeof(struct proto_ftp_priv));
		return POM_ERR;
	}
	memset(priv, 0, sizeof(struct proto_ftp_priv));

	proto_set_priv(proto, priv);

	// Register the ftp_cmd event
	static struct data_item_reg evt_cmd_data_items[PROTO_FTP_EVT_CMD_DATA_COUNT] = { { 0 } };
	evt_cmd_data_items[proto_ftp_cmd_name].name = "name";
	evt_cmd_data_items[proto_ftp_cmd_name].value_type = ptype_get_type("string");
	evt_cmd_data_items[proto_ftp_cmd_arg].name = "arg";
	evt_cmd_data_items[proto_ftp_cmd_arg].value_type = ptype_get_type("string");

	static struct data_reg evt_cmd_data = {
		.items = evt_cmd_data_items,
		.data_count = PROTO_FTP_EVT_CMD_DATA_COUNT
	};

	static struct event_reg_info proto_ftp_evt_cmd = { 0 };
	proto_ftp_evt_cmd.source_name = "proto_ftp";
	proto_ftp_evt_cmd.source_obj = proto;
	proto_ftp_evt_cmd.name = "ftp_cmd";
	proto_ftp_evt_cmd.description = "FTP command from the client";
	proto_ftp_evt_cmd.data_reg = &evt_cmd_data;

	priv->evt_cmd = event_register(&proto_ftp_evt_cmd);
	if (!priv->evt_cmd)
		goto err;

	// Register the ftp_reply event
	static struct data_item_reg evt_reply_data_items[PROTO_FTP_EVT_CMD_DATA_COUNT] = { { 0 } };
	evt_reply_data_items[proto_ftp_reply_code].name = "code";
	evt_reply_data_items[proto_ftp_reply_code].value_type = ptype_get_type("uint16");
	evt_reply_data_items[proto_ftp_reply_text].name = "text";
	evt_reply_data_items[proto_ftp_reply_text].flags = DATA_REG_FLAG_LIST;

	static struct data_reg evt_reply_data = {
		.items = evt_reply_data_items,
		.data_count = PROTO_FTP_EVT_REPLY_DATA_COUNT
	};

	static struct event_reg_info proto_ftp_evt_reply = { 0 };
	proto_ftp_evt_reply.source_name = "proto_ftp";
	proto_ftp_evt_reply.source_obj = proto;
	proto_ftp_evt_reply.name = "ftp_reply";
	proto_ftp_evt_reply.description = "FTP command from the client";
	proto_ftp_evt_reply.data_reg = &evt_reply_data;

	priv->evt_reply = event_register(&proto_ftp_evt_reply);
	if (!priv->evt_reply)
		goto err;

	return POM_OK;

err:
	proto_ftp_cleanup(priv);
	return POM_ERR;
}


static int proto_ftp_cleanup(void *proto_priv) {
	
	if (!proto_priv)
		return POM_OK;

	struct proto_ftp_priv *priv = proto_priv;
	if (priv->evt_cmd)
		event_unregister(priv->evt_cmd);
	if (priv->evt_reply)
		event_unregister(priv->evt_reply);

	free(priv);

	return POM_OK;
}

static int proto_ftp_process(void *proto_priv, struct packet *p, struct proto_process_stack *stack, unsigned int stack_index) {

	struct proto_process_stack *s = &stack[stack_index];
	struct proto_process_stack *s_next = &stack[stack_index + 1];

	if (conntrack_get_unique_from_parent(stack, stack_index) != POM_OK) {
		pomlog(POMLOG_ERR "Coult not get conntrack entry");
		return PROTO_ERR;
	}

	// There should no need to keep the lock here since we are in the packet_stream lock from proto_tcp
	conntrack_unlock(s->ce);

	struct proto_ftp_priv *ppriv = proto_priv;

	struct proto_ftp_conntrack_priv *priv = s->ce->priv;
	if (!priv) {
		priv = malloc(sizeof(struct proto_ftp_conntrack_priv));
		if (!priv) {
			pom_oom(sizeof(struct proto_ftp_conntrack_priv));
			return PROTO_ERR;
		}
		memset(priv, 0, sizeof(struct proto_ftp_conntrack_priv));

		priv->parser[POM_DIR_FWD] = packet_stream_parser_alloc(SMTP_MAX_LINE, PACKET_STREAM_PARSER_FLAG_TRIM);
		if (!priv->parser[POM_DIR_FWD]) {
			free(priv);
			return PROTO_ERR;
		}

		priv->parser[POM_DIR_REV] = packet_stream_parser_alloc(SMTP_MAX_LINE, PACKET_STREAM_PARSER_FLAG_TRIM);
		if (!priv->parser[POM_DIR_REV]) {
			packet_stream_parser_cleanup(priv->parser[POM_DIR_FWD]);
			free(priv);
			return PROTO_ERR;
		}

		priv->server_direction = POM_DIR_UNK;

		s->ce->priv = priv;
	}

	if (priv->flags & PROTO_FTP_FLAG_INVALID)
		return PROTO_OK;

	struct packet_stream_parser *parser = priv->parser[s->direction];
	if (packet_stream_parser_add_payload(parser, s->pload, s->plen) != POM_OK)
		return PROTO_ERR;

	char *line = NULL;
	unsigned int len = 0;
	while (1) {

		// Process commands
		if (packet_stream_parser_get_line(parser, &line, &len) != POM_OK)
			return PROTO_ERR;

		if (!line)
			return PROTO_OK;

		if (!len) // Probably a missed packet
			return PROTO_OK;

		// Try to find the server direction
		if (priv->server_direction == POM_DIR_UNK) {
			unsigned int code = atoi(line);
			if (code > 0) {
				priv->server_direction = s->direction;
			} else {
				priv->server_direction = POM_DIR_REVERSE(s->direction);
			}
		}

		if (s->direction == priv->server_direction) {

			// Parse the response code and generate the event
			if ((len < 5) || // Server response is 3 digit error code, a space or hyphen and then at least one letter of text
				(line[3] != ' ' && line[3] != '-')) {
				pomlog(POMLOG_DEBUG "Too short or invalid response from server");
				priv->flags |= PROTO_FTP_FLAG_INVALID;
				return POM_OK;
			}

			int code = atoi(line);
			if (code == 0) {
				pomlog(POMLOG_DEBUG "Invalid response from server");
				priv->flags |= PROTO_FTP_FLAG_INVALID;
				return POM_OK;
			}

			if (event_has_listener(ppriv->evt_reply)) {

				struct data *evt_data = NULL;
				if (priv->reply_evt) {
					evt_data = event_get_data(priv->reply_evt);
					uint16_t cur_code = *PTYPE_UINT16_GETVAL(evt_data[proto_ftp_reply_code].value);
					if (cur_code != code) {
						pomlog(POMLOG_WARN "Multiline code not the same as previous line : %hu -> %hu", cur_code, code);
						event_process_end(priv->reply_evt);
						priv->reply_evt = NULL;
					}
				}

				if (!priv->reply_evt) {
					priv->reply_evt = event_alloc(ppriv->evt_reply);
					if (!priv->reply_evt)
						return PROTO_ERR;

					evt_data = event_get_data(priv->reply_evt);
					PTYPE_UINT16_SETVAL(evt_data[proto_ftp_reply_code].value, code);
					data_set(evt_data[proto_ftp_reply_code]);

				}

				if (len > 4) {
					struct ptype *txt = ptype_alloc("string");
					if (!txt)
						return PROTO_ERR;
					PTYPE_STRING_SETVAL_N(txt, line + 4, len - 4);
					if (data_item_add_ptype(evt_data, proto_ftp_reply_text, strdup("text"), txt) != POM_OK)
						return PROTO_ERR;
				}
				
				if (!event_is_started(priv->reply_evt))
					event_process_begin(priv->reply_evt, stack, stack_index, p->ts);
			}

			if (line[3] != '-') {
				// Last line in the response
				if (priv->reply_evt) {
					event_process_end(priv->reply_evt);
					priv->reply_evt = NULL;
				}
			}

		} else {

			// Client command

			if (len < 4) { // Client commands are at least 4 bytes long
				pomlog(POMLOG_DEBUG "Too short or invalid query from client");
				priv->flags |= PROTO_FTP_FLAG_INVALID;
				return POM_OK;
			}

			// Make sure it's a command by checking it's at least a four letter word
			int i;
			for (i = 0; i < 4; i++) {
				// In some case it can also be a base64 encoded word
				if (! ((line[i] >= 'A' && line[i] <= 'Z')
					|| (line[i] >= 'a' && line[i] <= 'z')
					|| (line[i] >= '0' && line [i] <= '9')
					|| line[i] == '='))
					break;
			}

			if ((i < 4)) {
				pomlog(POMLOG_DEBUG "Received invalid client command");
				priv->flags |= PROTO_FTP_FLAG_INVALID;
				return POM_OK;
			}

			if (!strncasecmp(line, "DATA", strlen("DATA")) && len == strlen("DATA")) {
				priv->flags |= PROTO_FTP_FLAG_CLIENT_DATA;
			}

			if (event_has_listener(ppriv->evt_cmd)) {
				struct event *evt = event_alloc(ppriv->evt_cmd);
				if (!evt)
					return PROTO_ERR;

				size_t cmdlen = len;
				char *space = memchr(line, ' ', len);
				if (space)
					cmdlen = space - line;

				struct data *evt_data = event_get_data(evt);
				PTYPE_STRING_SETVAL_N(evt_data[proto_ftp_cmd_name].value, line, cmdlen);
				data_set(evt_data[proto_ftp_cmd_name]);
				if (space) {
					PTYPE_STRING_SETVAL_N(evt_data[proto_ftp_cmd_arg].value, space + 1, len - 1 - cmdlen);
					data_set(evt_data[proto_ftp_cmd_arg]);
				}

				if (priv->flags & PROTO_FTP_FLAG_CLIENT_DATA) {
					// The event ends at the end of the message
					priv->data_evt = evt;
					return event_process_begin(evt, stack, stack_index, p->ts);
				} else {
					return event_process(evt, stack, stack_index, p->ts);
				}
			}

		}



	}

	return PROTO_OK;

}

static int proto_ftp_post_process(void *proto_priv, struct packet *p, struct proto_process_stack *stack, unsigned int stack_index) {

	struct conntrack_entry *ce = stack[stack_index].ce;
	struct proto_ftp_conntrack_priv *priv = ce->priv;

	if (!priv)
		return PROTO_OK;
	
	if (priv->flags & PROTO_FTP_FLAG_CLIENT_DATA_END) {
		if (priv->data_evt) {
			if (event_process_end(priv->data_evt) != POM_OK)
				return PROTO_ERR;
			priv->data_evt = NULL;
		}
		priv->flags &= ~PROTO_FTP_FLAG_CLIENT_DATA_END;
		priv->data_end_pos = 0;
	}

	return POM_OK;
}

static int proto_ftp_conntrack_cleanup(void *ce_priv) {

	struct proto_ftp_conntrack_priv *priv = ce_priv;
	if (!priv)
		return POM_OK;

	if (priv->parser[POM_DIR_FWD])
		packet_stream_parser_cleanup(priv->parser[POM_DIR_FWD]);

	if (priv->parser[POM_DIR_REV])
		packet_stream_parser_cleanup(priv->parser[POM_DIR_REV]);

	if (priv->data_evt) {
		if (event_is_started(priv->data_evt))
			event_process_end(priv->data_evt);
		else
			event_cleanup(priv->data_evt);
	}

	if (priv->reply_evt) {
		if (event_is_started(priv->reply_evt))
			event_process_end(priv->reply_evt);
		else
			event_cleanup(priv->reply_evt);
	}
		
	free(priv);

	return POM_OK;
}

static int proto_ftp_mod_unregister() {

	return proto_unregister("ftp");

}
