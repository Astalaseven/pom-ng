/*
 *  This file is part of pom-ng.
 *  Copyright (C) 2011 Guy Martin <gmsoft@tuxicoman.be>
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

#include <sys/time.h>
#include <stdio.h>
#include "analyzer_http.h"
#include "analyzer_http_post.h"
#include <pom-ng/proto_http.h>
#include <pom-ng/ptype_uint16.h>
#include <pom-ng/ptype_string.h>

static struct ptype *ptype_string = NULL;

struct mod_reg_info* analyzer_http_reg_info() {

	static struct mod_reg_info reg_info;
	memset(&reg_info, 0, sizeof(struct mod_reg_info));
	reg_info.api_ver = MOD_API_VER;
	reg_info.register_func = analyzer_http_mod_register;
	reg_info.unregister_func = analyzer_http_mod_unregister;

	return &reg_info;
}


int analyzer_http_mod_register(struct mod_reg *mod) {

	static struct analyzer_reg analyzer_http;
	memset(&analyzer_http, 0, sizeof(struct analyzer_reg));
	analyzer_http.name = "http";
	analyzer_http.api_ver = ANALYZER_API_VER;
	analyzer_http.mod = mod;
	analyzer_http.init = analyzer_http_init;
	analyzer_http.cleanup = analyzer_http_cleanup;

	return analyzer_register(&analyzer_http);

}

int analyzer_http_mod_unregister() {

	int res = analyzer_unregister("http");

	return res;
}


int analyzer_http_init(struct analyzer *analyzer) {


	ptype_string = ptype_alloc("string");
	if (!ptype_string)
		return POM_ERR;

	
	struct analyzer_http_priv *priv = malloc(sizeof(struct analyzer_http_priv));
	if (!priv) {
		pom_oom(sizeof(struct analyzer_http_priv));
		return POM_ERR;
	}
	memset(priv, 0, sizeof(struct analyzer_http_priv));

	static struct analyzer_data_reg evt_request_data[ANALYZER_HTTP_EVT_REQUEST_DATA_COUNT + 1];
	memset(&evt_request_data, 0, sizeof(struct analyzer_data_reg) * (ANALYZER_HTTP_EVT_REQUEST_DATA_COUNT + 1));
	evt_request_data[analyzer_http_request_server_name].name = "server_name";
	evt_request_data[analyzer_http_request_server_addr].name = "server_addr";
	evt_request_data[analyzer_http_request_server_port].name = "server_port";
	evt_request_data[analyzer_http_request_client_addr].name = "client_addr";
	evt_request_data[analyzer_http_request_client_port].name = "client_port";
	evt_request_data[analyzer_http_request_request_proto].name = "request_proto";
	evt_request_data[analyzer_http_request_request_method].name = "request_method";
	evt_request_data[analyzer_http_request_first_line].name = "first_line";
	evt_request_data[analyzer_http_request_url].name = "url";
	evt_request_data[analyzer_http_request_query_time].name = "query_time";
	evt_request_data[analyzer_http_request_response_time].name = "response_time";
	evt_request_data[analyzer_http_request_username].name = "username";
	evt_request_data[analyzer_http_request_password].name = "password";
	evt_request_data[analyzer_http_request_status].name = "status";
	evt_request_data[analyzer_http_request_query_headers].name = "query_headers";
	evt_request_data[analyzer_http_request_query_headers].flags = ANALYZER_DATA_FLAG_LIST;
	evt_request_data[analyzer_http_request_query_headers].value_template = ptype_string;
	evt_request_data[analyzer_http_request_response_headers].name = "response_headers";
	evt_request_data[analyzer_http_request_response_headers].flags = ANALYZER_DATA_FLAG_LIST;
	evt_request_data[analyzer_http_request_response_headers].value_template = ptype_string;
	evt_request_data[analyzer_http_request_post_data].name = "post_data";
	evt_request_data[analyzer_http_request_post_data].flags = ANALYZER_DATA_FLAG_LIST;
	evt_request_data[analyzer_http_request_post_data].value_template = ptype_string;


	if (analyzer_event_register(analyzer, "http_request", evt_request_data, analyzer_http_event_listeners_notify) == NULL) {
		free(priv);
		return POM_ERR;
	}

	priv->proto_http = proto_add_dependency("http");
	if (!priv->proto_http) {
		free(priv);
		return POM_ERR;
	}
	analyzer->priv = priv;

	return analyzer_http_post_init(analyzer);
}

int analyzer_http_cleanup(struct analyzer *analyzer) {

	struct analyzer_http_priv *priv = analyzer->priv;
	proto_remove_dependency(priv->proto_http);

	free(priv);

	if (ptype_string) {
		ptype_cleanup(ptype_string);
		ptype_string = NULL;
	}

	return POM_OK;
}

int analyzer_http_ce_priv_cleanup(void *obj, void *priv) {

	struct analyzer_http_ce_priv *p = priv;
	analyzer_http_event_reset(&p->evt);
	free(p->evt.data);

	int i;
	for (i = 0; i < 2; i++) {
		if (p->pload[i]) {
			analyzer_pload_buffer_cleanup(p->pload[i]);
			p->pload[i] = NULL;
		}
	}

	free(p);

	return POM_OK;
}

int analyzer_http_event_listeners_notify(struct analyzer *analyzer, struct analyzer_event_reg *evt_reg, int has_listeners) {

	struct analyzer_http_priv *priv = analyzer->priv;

	if (has_listeners) {
		static struct proto_event_analyzer_reg analyzer_reg;
		analyzer_reg.analyzer = analyzer;
		analyzer_reg.process = analyzer_http_proto_event_process;
		analyzer_reg.expire = analyzer_http_proto_event_expire;
		if (proto_event_analyzer_register(priv->proto_http->proto, &analyzer_reg) != POM_OK)
			return POM_ERR;

		priv->http_packet_listener = proto_packet_listener_register(priv->proto_http->proto, PROTO_PACKET_LISTENER_PLOAD_ONLY, analyzer, analyzer_http_proto_packet_process);
		if (!priv->http_packet_listener) {
			proto_event_analyzer_unregister(priv->proto_http->proto, analyzer);
			return POM_ERR;
		}

	} else {
		if (proto_packet_listener_unregister(priv->http_packet_listener) != POM_OK)
			return POM_ERR;
		return proto_event_analyzer_unregister(priv->proto_http->proto, analyzer);
	}

	return POM_OK;
}

int analyzer_http_event_reset(struct analyzer_event *evt) {

	// Free possibly allocated stuff
	struct analyzer_data *data = evt->data;
	if (data[analyzer_http_request_server_addr].value)
		ptype_cleanup(data[analyzer_http_request_server_addr].value);
	if (data[analyzer_http_request_server_port].value)
		ptype_cleanup(data[analyzer_http_request_server_port].value);
	if (data[analyzer_http_request_client_addr].value)
		ptype_cleanup(data[analyzer_http_request_client_addr].value);
	if (data[analyzer_http_request_client_port].value)
		ptype_cleanup(data[analyzer_http_request_client_port].value);

	while (data[analyzer_http_request_post_data].items) {
		analyzer_data_item_t *tmp = data[analyzer_http_request_post_data].items;
		data[analyzer_http_request_post_data].items = tmp->next;

		free(tmp->key);
		ptype_cleanup(tmp->value);
		free(tmp);
	}

	memset(data, 0, sizeof(struct analyzer_data) * (ANALYZER_HTTP_EVT_REQUEST_DATA_COUNT + 1));

	return POM_OK;
}

int analyzer_http_proto_event_process(struct analyzer *analyzer, struct proto_event *evt, struct proto_process_stack *stack, unsigned int stack_index) {

	struct proto_process_stack *s = &stack[stack_index];
	if (!s->ce)
		return POM_ERR;

	struct analyzer_http_ce_priv *priv = conntrack_get_priv(s->ce, analyzer);
	if (!priv) {
		priv = malloc(sizeof(struct analyzer_http_ce_priv));
		if (!priv) {
			pom_oom(sizeof(struct analyzer_http_ce_priv));
			return POM_ERR;
		}
		memset(priv, 0, sizeof(struct analyzer_http_ce_priv));

		priv->evt.info = analyzer->events;

		struct analyzer_data *data = malloc(sizeof(struct analyzer_data) * (ANALYZER_HTTP_EVT_REQUEST_DATA_COUNT + 1));
		if (!data) {
			free(priv);
			pom_oom(sizeof(struct analyzer_data) * (ANALYZER_HTTP_EVT_REQUEST_DATA_COUNT + 1));
			return POM_ERR;
		}
		memset(data, 0, sizeof(struct analyzer_data) * (ANALYZER_HTTP_EVT_REQUEST_DATA_COUNT + 1));
		priv->evt.data = data;

		if (conntrack_add_priv(s->ce, analyzer, priv, analyzer_http_ce_priv_cleanup) != POM_OK)
			return POM_ERR;

	}

	// Do the mapping, no flag checking or other, we just know how :)

	struct proto_event_data *src_data = evt->data;
	struct analyzer_data *dst_data = priv->evt.data;

	analyzer_data_item_t *headers = NULL;

	if (evt->evt_reg->id == proto_http_evt_query) {

		priv->flags |= ANALYZER_HTTP_GOT_QUERY_EVT;

		priv->query_dir = s->direction;

		// Copy data contained into the query event
		if (src_data[proto_http_query_first_line].set)
			dst_data[analyzer_http_request_first_line].value = src_data[proto_http_query_first_line].value;
		if (src_data[proto_http_query_proto].set)
			dst_data[analyzer_http_request_request_proto].value = src_data[proto_http_query_proto].value;
		if (src_data[proto_http_query_method].set)
			dst_data[analyzer_http_request_request_method].value = src_data[proto_http_query_method].value;
		if (src_data[proto_http_query_url].set)
			dst_data[analyzer_http_request_url].value = src_data[proto_http_query_url].value;
		if (src_data[proto_http_query_start_time].set)
			dst_data[analyzer_http_request_query_time].value = src_data[proto_http_query_start_time].value;

		dst_data[analyzer_http_request_query_headers].items = src_data[proto_http_query_headers].items;

		headers = src_data[proto_http_query_headers].items;


	} else if (evt->evt_reg->id == proto_http_evt_response) {

		priv->flags |= ANALYZER_HTTP_GOT_RESPONSE_EVT;
		priv->query_dir = (s->direction == CT_DIR_FWD ? CT_DIR_REV : CT_DIR_FWD);

		if (src_data[proto_http_response_status].set)
			dst_data[analyzer_http_request_status].value = src_data[proto_http_response_status].value;
		if (!dst_data[analyzer_http_request_request_proto].value && src_data[proto_http_response_proto].set)
			dst_data[analyzer_http_request_request_proto].value = src_data[proto_http_response_proto].value;
		if (src_data[proto_http_response_start_time].set)
			dst_data[analyzer_http_request_response_time].value = src_data[proto_http_response_start_time].value;

		dst_data[analyzer_http_request_response_headers].items = src_data[proto_http_response_headers].items;

		headers = src_data[proto_http_response_headers].items;


	} else {
		pomlog(POMLOG_ERR "Unknown event ID %u", evt->evt_reg->id);
		return POM_ERR;
	}

	// Parse the info we need from the header
	for (; headers; headers = headers->next) {
		if (evt->evt_reg->id == proto_http_evt_query) {

			if (!dst_data[analyzer_http_request_server_name].value && !strcasecmp(headers->key, "Host")) {
				dst_data[analyzer_http_request_server_name].value = headers->value;
				continue;
			}
	
			// TODO username and password
		}


		if (!strcasecmp(headers->key, "Content-Length")) {
			size_t content_len = 0;
			if (sscanf(PTYPE_STRING_GETVAL(headers->value), "%zu", &content_len) != 1) {
				pomlog(POMLOG_DEBUG "Could not parse Content-Length \"%s\"", PTYPE_STRING_GETVAL(headers->value));
				continue;
			}
			priv->content_len[s->direction] = content_len;
		} else if (!strcasecmp(headers->key, "Content-Type")) {
			priv->content_type[s->direction] = PTYPE_STRING_GETVAL(headers->value);
		}
		


	}

	// Get client/server ports if not fetched yet
	if (stack_index > 1 && (!dst_data[analyzer_http_request_client_port].value || !dst_data[analyzer_http_request_server_port].value)) {
		struct proto_process_stack *l4_stack = &stack[stack_index - 1];
		struct ptype *sport = NULL, *dport = NULL;
		unsigned int i;
		for (i = 0; !sport || !dport; i++) {
			char *name = l4_stack->proto->info->pkt_fields[i].name;
			if (!name)
				break;
			if (!sport && !strcmp(name, "sport"))
				sport = l4_stack->pkt_info->fields_value[i];
			else if (!dport && !strcmp(name, "dport"))
				dport = l4_stack->pkt_info->fields_value[i];
		}

		if (evt->evt_reg->id == proto_http_evt_query) {
			if (sport && !dst_data[analyzer_http_request_client_port].value)
				dst_data[analyzer_http_request_client_port].value = ptype_alloc_from(sport);
			if (dport && !dst_data[analyzer_http_request_server_port].value)
				dst_data[analyzer_http_request_server_port].value = ptype_alloc_from(dport);
		} else {
			if (sport && !dst_data[analyzer_http_request_server_port].value)
				dst_data[analyzer_http_request_server_port].value = ptype_alloc_from(sport);
			if (dport && !dst_data[analyzer_http_request_client_port].value)
				dst_data[analyzer_http_request_client_port].value = ptype_alloc_from(dport);
		}
	}

	if (stack_index > 2 && (!dst_data[analyzer_http_request_client_addr].value || !dst_data[analyzer_http_request_server_addr].value)) {
		struct ptype *src = NULL, *dst = NULL;
		struct proto_process_stack *l3_stack = &stack[stack_index - 2];
		unsigned int i;
		for (i = 0; !src || !dst ; i++) {
			char *name = l3_stack->proto->info->pkt_fields[i].name;
			if (!name)
				break;

			if (!src && !strcmp(name, "src"))
				src = l3_stack->pkt_info->fields_value[i];
			else if (!dst && !strcmp(name, "dst"))
				dst = l3_stack->pkt_info->fields_value[i];
		}

		if (evt->evt_reg->id == proto_http_evt_query) {
			if (src && !dst_data[analyzer_http_request_client_addr].value)
				dst_data[analyzer_http_request_client_addr].value = ptype_alloc_from(src);
			if (dst && !dst_data[analyzer_http_request_server_addr].value)
				dst_data[analyzer_http_request_server_addr].value = ptype_alloc_from(dst);
		} else {
			if (src && !dst_data[analyzer_http_request_server_addr].value)
				dst_data[analyzer_http_request_server_addr].value = ptype_alloc_from(src);
			if (dst && !dst_data[analyzer_http_request_client_addr].value)
				dst_data[analyzer_http_request_client_addr].value = ptype_alloc_from(dst);
		}
	}

	if ((priv->flags & (ANALYZER_HTTP_GOT_QUERY_EVT | ANALYZER_HTTP_GOT_RESPONSE_EVT)) == (ANALYZER_HTTP_GOT_QUERY_EVT | ANALYZER_HTTP_GOT_RESPONSE_EVT)) {
		// We got both events, process our composite event
		int result = analyzer_event_process(&priv->evt);
		priv->flags &= ~(ANALYZER_HTTP_GOT_QUERY_EVT | ANALYZER_HTTP_GOT_RESPONSE_EVT);
		analyzer_http_event_reset(&priv->evt);
		return result;
	}

	return POM_OK;
}

int analyzer_http_proto_event_expire(struct analyzer *analyzer, struct proto_event *evt, struct conntrack_entry *ce) {


	struct analyzer_http_ce_priv *priv = conntrack_get_priv(ce, analyzer);
	if (!priv)
		return POM_OK;

	if (priv->flags & (ANALYZER_HTTP_GOT_QUERY_EVT | ANALYZER_HTTP_GOT_RESPONSE_EVT)) {
		// We either got a request or a query. Process
		int result = analyzer_event_process(&priv->evt);
		priv->flags &= ~(ANALYZER_HTTP_GOT_QUERY_EVT | ANALYZER_HTTP_GOT_RESPONSE_EVT);
		analyzer_http_event_reset(&priv->evt);
		return result;
	}

	int i;
	for (i = 0; i < 2; i++) {
		if (priv->pload[i]) {
			analyzer_pload_buffer_cleanup(priv->pload[i]);
			priv->pload[i] = NULL;
		}
		priv->content_len[i] = 0;
		priv->content_type[i] = NULL;
	}


	return POM_OK;
}

int analyzer_http_proto_packet_process(void *object, struct packet *p, struct proto_process_stack *stack, unsigned int stack_index) {

	struct analyzer *analyzer = object;

	struct proto_process_stack *pload_stack = &stack[stack_index];

	struct proto_process_stack *s = &stack[stack_index - 1];
	if (!s->ce)
		return POM_ERR;

	struct analyzer_http_ce_priv *priv = conntrack_get_priv(s->ce, analyzer);
	if (!priv) {
		pomlog(POMLOG_ERR "No private data attached to this connection. Ignoring payload.");
		return POM_ERR;
	}

	int dir = s->direction;

	struct analyzer_pload_type *type = analyzer_pload_type_get_by_mime_type(priv->content_type[dir]);

	if (!priv->pload[dir]) {
		priv->pload[dir] = analyzer_pload_buffer_alloc(type, priv->content_len[dir], ANALYZER_PLOAD_BUFFER_NEED_MAGIC);
		if (!priv->pload[dir])
			return POM_ERR;

		priv->pload[dir]->rel_event = &priv->evt;

	}

	pomlog("Got %u of HTTP payload", s->plen);

	if (analyzer_pload_buffer_append(priv->pload[dir], pload_stack->pload, pload_stack->plen) != POM_OK)
		return POM_ERR;

	return POM_OK;
}
