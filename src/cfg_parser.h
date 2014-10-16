/*
 * $Id: cfg_parser.h,v 1.5 2003/04/22 19:58:41 andrei Exp $
 *
 * Copyright (C) 2002-2003 Fhg Fokus
 *
 * This file is part of disc, a free diameter server/client.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License 
 * along with this program; if not, write to the Free Software 
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 * History:
 * --------
 *  2003-03-13  created by andrei
 *  2003-04-07  added cfg_callback (andrei)
 */

#ifndef  cfg_parser_h
#define cfg_parser_h

#include <stdio.h>
#include "str.h"

#define CFG_EMPTY   0
#define CFG_COMMENT 1
#define CFG_SKIP    2
#define CFG_DEF     3
#define CFG_ERROR  -1

#define MAX_LINE_SIZE 800

#define CFG_TOKENS 100 /* max numbers of tokens on a line */

enum cfg_errors { CFG_OK=0, CFG_ERR=-1, CFG_PARAM_ERR=-2, CFG_RUN_ERR=-3,
                  CFG_MEM_ERR };

struct cfg_line{
	int type;
	char* id;
	int has_equal; /* 1 if line is id = something */
	char* value[CFG_TOKENS];
	int token_no; /* number of value token parsed */
};


enum cfg_def_types {INT_VAL, STR_VAL, GEN_VAL };
typedef    int (*cfg_callback)(struct cfg_line*, void* value) ;

struct cfg_def{
	char* name;
	enum cfg_def_types type;
	void* value;
	cfg_callback c;
};


extern struct cfg_def cfg_ids[]; /* null terminated array */



int cfg_parse_line(char* line, struct cfg_line* cl);
int cfg_parse_stream(FILE* stream);
int cfg_run_def(struct cfg_line* cl);

int cfg_getstr(char* p, str* r);
int cfg_getint(char* p, int* i);

#endif
