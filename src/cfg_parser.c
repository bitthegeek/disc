/*
 * $Id: cfg_parser.c,v 1.7 2003/04/22 21:47:10 andrei Exp $
 *
 * configuration parser
 *
 * Config file format:
 *
 *  id = value    # comment
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
 *  2003-04-07  added multiple token support and callbacks (andrei)
 */


#include <errno.h>
#include <string.h>
#include <stdlib.h>

#include "cfg_parser.h"
#include "dprint.h"
#include "parser_f.h"
#include "mem/shm_mem.h"




/* params: null terminated text line => fills cl
 * returns 0, or on error -1. */
int cfg_parse_line(char* line, struct cfg_line* cl)
{
	/* format:
		line = expr | comment
		comment = SP* '#'.*
		expr = SP* id SP* '=' SP* value ... comment?
	*/
		
	char* tmp;
	char* end;
	char* t;
	int r;

#define end_test(x, e) ((x)>=(e) || *(x)=='#' || *(x)=='\n' || *(x)=='\r')
	
	memset(cl, 0, sizeof(*cl));
	end=line+strlen(line);
	tmp=eat_space_end(line, end);
	if ((end_test(tmp, end))||(is_empty_end(tmp, end))) {
		cl->type=CFG_EMPTY;
		goto skip;
	}
	if (*tmp=='#'){
		cl->type=CFG_COMMENT;
		goto skip;
	}
	cl->id=tmp;
	tmp=eat_token2_end(cl->id,end, '=');
	if (end_test(tmp, end)) goto error;
	t=tmp;
	tmp=eat_space_end(tmp,end);
	if (end_test(tmp, end)) goto error;
	if (*tmp=='=') {cl->has_equal=1; tmp++;};
	*t=0; /* zero terminate*/
	
	for (r=0; r<CFG_TOKENS; r++){
		tmp=eat_space_end(tmp,end);
		if (end_test(tmp, end)) goto end;
		t=tmp;
		tmp=eat_token_end(t, end);
		if (tmp<end) {*tmp=0; tmp++;} /* zero terminate*/
		cl->value[r]=t;
		cl->token_no++;
		/*
		printf (" token %d (%d) <%s>, *%x (%x, %x) \n", r, cl->token_no, t,
				*tmp, tmp, end);
		*/
	}
	if (tmp+1<end){
		if (!is_empty_end(tmp+1,end)){
			/* check if comment */
			tmp=eat_space_end(tmp+1, end);
			if (!end_test(tmp,end)){
					/* extra chars at the end of line */
					goto error_extra_tokens;
			}
		}
	}
	
end:	
	cl->type=CFG_DEF;
skip:
	return 0;
error_extra_tokens:
	LOG(L_CRIT, "ERROR: too many  tokens");
error:
	cl->type=CFG_ERROR;
	return -1;
}



/* parses the cfg, returns 0 on success, line no otherwise */
int cfg_parse_stream(FILE* stream)
{
	int line;
	struct cfg_line cl;
	char buf[MAX_LINE_SIZE];
	int ret;

	line=1;
	while(!feof(stream)){
		if (fgets(buf, MAX_LINE_SIZE, stream)){
			cfg_parse_line(buf, &cl);
			switch (cl.type){
				case CFG_DEF:
					if ((ret=cfg_run_def(&cl))!=0){
						if (log_stderr==0){
							fprintf(stderr, 
									"ERROR (%d): on line %d\n", ret, line);
						}
						LOG(L_CRIT, "ERROR (%d): on line %d\n", ret, line);
						goto error;
					}
					break;
				case CFG_COMMENT:
				case CFG_SKIP:
					break;
				case CFG_ERROR:
					if (log_stderr==0){
						fprintf(stderr, 
								"ERROR: bad config line (%d):%s\n", line, buf);
					}
					LOG(L_CRIT, "ERROR: bad config line (%d):%s\n", line, buf);
					goto error;
					break;
			}
			line++;
		}else{
			if (ferror(stream)){
				if (log_stderr==0){
					fprintf(stderr, 
							"ERROR: reading configuration: %s\n",
							strerror(errno));
				}
				LOG(L_CRIT,
						"ERROR: reading configuration: %s\n",
						strerror(errno));
				goto error;
			}
			break;
		}
	}
	return 0;

error:
	return line;
}



int cfg_getint(char* p, int* i)
{
	char* end;

	*i=strtol(p,&end ,10);
	if (*end) return -3;
	else return 0;
}


int cfg_getstr(char* p, str* r)
{
	int quotes;
	char* s;
	int len;
	
	quotes=0;
	s=0;
	if (*p=='"') { p++; quotes++; };
	for (; *p; p++){
		if (*p=='"') quotes++;
		if (s==0) s=p;
	}
	if (quotes%2) return -3; /* bad quote number */
	if (quotes){
		if (*(p-1)!='"') return -3; /* not terminated by quotes */
		len=p-1-s;
		*(p-1)=0;
	}else{
		len=p-s;
	}
	r->s=(char*)shm_malloc(len+1);
	if (r->s==0) return -4; /* mem. alloc. error */
	memcpy(r->s, s, len);
	r->s[len]=0; /* null terminate, we are counting on this :-) */
	r->len=len;
	return 0;
}



int cfg_run_def(struct cfg_line *cl)
{
	struct cfg_def* def;
		
	
	for(def=cfg_ids; def && def->name; def++)
		if (strcasecmp(cl->id, def->name)==0){
			switch(def->type){
				case INT_VAL:
					if (cl->has_equal==0){
						LOG(L_CRIT, "missing '=' ?\n");
						return -2;
					}
					if (cl->token_no>1){
						LOG(L_CRIT, "single int value expected -- "
								"too many tokens\n");
						return -2;
					}
					if (def->c) return def->c(cl, def->value);
					else return cfg_getint(cl->value[0], def->value);
					break;
				case STR_VAL:
					if (cl->has_equal==0){
						LOG(L_CRIT, "missing '=' ?\n");
						return -2;
					}
					if (cl->token_no>1){
						LOG(L_CRIT, "single string value expected -- "
								"too many tokens\n");
						return -2;
					}
					if (def->c) return def->c(cl, def->value);
					else return cfg_getstr(cl->value[0], def->value);
					break;
				case GEN_VAL:
					if (def->c) return def->c(cl, def->value);
					else return 0; /* ignore the line */
					break;
				default:
					LOG(L_CRIT, "BUG: cfg_run_def: unknown type %d\n",
							def->type);
					return -2;
			}
		}
	/* not found */
	LOG(L_CRIT, "ERROR: unknown id <%s>\n", cl->id);
	return -1;
}


