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

#ifndef __OUTPUT_LOG_H__

#include <pom-ng/analyzer.h>
#include <pom-ng/output.h>

struct output_log_parsed_field {
	int id;
	uint32_t hash;
	char *key;
	unsigned int start_off, end_off;
};

struct output_log_txt_priv {
	int fd;
	struct ptype *p_filename;
	struct ptype *p_source;
	struct ptype *p_format;
	struct output_log_parsed_field *parsed_fields;

	struct analyzer_data_source *src;
	unsigned int field_count;
};

struct mod_reg_info* output_log_reg_info();
static int output_log_mod_register(struct mod_reg *mod);
static int output_log_mod_unregister();


static int output_log_txt_init(struct output *o);
static int output_log_txt_open(struct output *o);
static int output_log_txt_close(struct output *o);
static int output_log_txt_cleanup(struct output *o);
static int output_log_txt_process(struct output *o, struct analyzer_data *src);

#endif
