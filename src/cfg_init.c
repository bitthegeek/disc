/*
 * $Id: cfg_init.c,v 1.13 2003/04/22 21:47:10 andrei Exp $
 *
 * History:
 * --------
 * 2003-03-07  created mostly from pieces of diameter_api/init_conf.c by andrei
 * 2003-04-16  lots of new startup parameters added (andrei)
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


#include <errno.h>
#include <string.h>

#include "../libltdl/ltdl.h"

#include "diameter_api/diameter_api.h"
#include "dprint.h"
#include "cfg_init.h"
#include "cfg_parser.h"
#include "globals.h"
#include "aaa_module.h"
#include "aaa_parse_uri.h"
#include "mem/shm_mem.h"
#include "route.h"





static int cfg_set_module_path (struct cfg_line* cl, void* value);
static int cfg_load_modules(struct cfg_line* cl, void* value);
static int cfg_addpair(struct cfg_line* cl, void* callback);
static int cfg_echo(struct cfg_line* cl, void* value);
static int cfg_error(struct cfg_line* cl, void* value);
static int cfg_include(struct cfg_line* cl, void* value);
static int cfg_set_aaa_status(struct cfg_line* cl, void* value);
static int cfg_set_mod_param(struct cfg_line* cl, void* unused);



/* config info (null terminated array)*/
struct cfg_def cfg_ids[]={
	{"debug",         INT_VAL,   &debug,           0                   },
	{"log_stderr",    INT_VAL,   &log_stderr,      0                   },
	{"aaa_realm",     STR_VAL,   &aaa_realm,       0                   },
	{"aaa_fqdn",      STR_VAL,   &aaa_fqdn,        0                   },
	{"listen_port",   INT_VAL,   &listen_port,     0                   },
	{"worker_threads",INT_VAL,   &worker_threads,  0                   },
	{"reader_threads",INT_VAL,   &reader_threads,  0                   },
	{"aaa_status",    STR_VAL,   0,                cfg_set_aaa_status  },
	{"dont_fork",     INT_VAL,   &dont_fork,       0                   },
	{"chroot",        STR_VAL,   &chroot_dir,      0                   },
	{"workdir",       STR_VAL,   &working_dir,     0                   },
	{"user",          STR_VAL,   &user,            0                   },
	{"group",         STR_VAL,   &group,           0                   },
	{"pid_file",      STR_VAL,   &pid_file,        0                   },
	{"module_path",   STR_VAL,   &module_path,     cfg_set_module_path },
	{"set_mod_param", GEN_VAL,   0,                cfg_set_mod_param   },
	{"module",        GEN_VAL,   0,                cfg_load_modules    },
	{"peer",          GEN_VAL,   add_cfg_peer,     cfg_addpair         },
	{"route",         GEN_VAL,   add_route,        cfg_addpair         },
	{"echo",          GEN_VAL,   0,                cfg_echo            },
	{"_error",        GEN_VAL,   0,                cfg_error           },
	{"include",       STR_VAL,   0,                cfg_include         },
	{0,0,0,0}
};




int read_config_file( char *cfg)
{
	FILE* cfg_file;
	
	/* read the parameters from confg file */
	if ((cfg_file=fopen(cfg, "r"))==0){
		if (log_stderr==0){
			/* print message also to stderr */
			fprintf(stderr, "ERROR:read_config_file: reading config "
					"file %s: %s\n", cfg, strerror(errno));
		}
		LOG(L_CRIT,"ERROR:read_config_file: reading config "
					"file %s: %s\n", cfg, strerror(errno));
		goto error;
	}
	if (cfg_parse_stream(cfg_file)!=0){
		fclose(cfg_file);
		/*
		LOG(L_CRIT,"ERROR:read_config_file : reading "
					"config file(%s)\n", cfg);
		*/
		goto error;
	}
	fclose(cfg_file);
	
	return 0;
error:
	return -1;
}



int cfg_set_module_path(struct cfg_line* cl, void* value)
{
	int ret;
	str* s;
	
	if (cl->token_no!=1){
		LOG(L_CRIT, "ERROR: too many parameters for module path\n");
		return CFG_PARAM_ERR;
	}
	s=(str*)value;
	/* get the string */
	ret=cfg_getstr(cl->value[0], s);
	if (ret!=0) goto error;
	/* modules should be already init here */
	ret=lt_dlsetsearchpath(s->s);
	if (ret){
		LOG(L_CRIT, "ERROR:cfg_set_module_path:"
					" lt_dlsetsearchpath failed: %s\n", lt_dlerror());
		ret=CFG_RUN_ERR;
		goto error;
	}
	return CFG_OK;
error:
	return ret;
}



int cfg_load_modules(struct cfg_line* cl, void* value)
{
	int ret;
	int r;
	str s;
	
	ret=CFG_PARAM_ERR;
	for (r=0; r<cl->token_no; r++){
		LOG(L_INFO, "loading module  %s...", cl->value[r]);
		ret=cfg_getstr(cl->value[r], &s);
		if (ret!=0){
			LOG(L_CRIT, "ERROR: load module: bad module name %s\n",
					cl->value[r]);
			break;
		}
		ret=load_module(s.s);
		if (ret==0) LOG(L_INFO, "ok\n");
		else LOG(L_INFO, "FAILED\n");
	}
	return ret;
}



int cfg_echo(struct cfg_line* cl, void* value)
{
	int r;
	
	for (r=0; r<cl->token_no; r++){
		LOG(L_INFO, "%s ", cl->value[r]);
	};
	LOG(L_INFO, "\n");
	return CFG_OK;
}




int cfg_error(struct cfg_line* cl, void* value)
{
	int r;
	
	for (r=0; r<cl->token_no; r++){
		LOG(L_CRIT, "%s ", cl->value[r]);
	};
	LOG(L_CRIT, "\n");
	return CFG_ERR;
}




int cfg_include(struct cfg_line* cl, void* value)
{
	str s;
	int ret;
	
	if (cl->token_no!=1){
		LOG(L_CRIT, "ERROR: too many parameters for include\n");
		return CFG_PARAM_ERR;
	}
	ret=cfg_getstr(cl->value[0], &s);
	if (ret!=0) return ret;
	DBG("-> including <%s>\n", s.s);
	ret=read_config_file(s.s);
	shm_free(s.s);
	return ret;
}



int cfg_set_aaa_status(struct cfg_line* cl, void* value)
{
	char* status_str[] =
		{"aaa_client","aaa_server","aaa_server_statefull"};
	unsigned int status_int[] =
		{AAA_CLIENT,AAA_SERVER,AAA_SERVER_STATEFULL};
	int i;

	if (cl->token_no!=1){
		LOG(L_CRIT, "ERROR: too many parameters for aaa_status\n");
		return CFG_PARAM_ERR;
	}
	if (cl->has_equal!=1){
		LOG(L_CRIT, "ERROR: missing equal for aaa_status\n");
		return CFG_PARAM_ERR;
	}
	for( i=(sizeof(status_str)/sizeof(char*))-2 ; i>=0 ; i--)
		if (!strcasecmp(cl->value[0],status_str[i]) ) {
			my_aaa_status = status_int[i];
			return CFG_OK;
		}
	LOG(L_CRIT,"ERROR: unknown \"%s\" value for aaa_status\n"
		"\tSupported values: AAA_SERVER, AAA_CLIENT\n",cl->value[0]);
	return CFG_ERR;
}



/* reads a str pair and calls callback(s1, s2) */
int cfg_addpair(struct cfg_line* cl, void* callback)
{
	int ret;
	str* s1;
	str* s2;
		
	s1=0;
	s2=0;
	if ((cl->token_no<1)||(cl->token_no>2)){
		LOG(L_CRIT, "ERROR: cfg: invalid number of "
					"parameters for peer\n");
		ret=CFG_PARAM_ERR;
		goto error;
	}
	s1=shm_malloc(sizeof(str));
	if (s1==0) goto error_mem;
	s2=shm_malloc(sizeof(str));
	if (s2==0) goto error_mem;
	memset(s2, 0, sizeof(str));
	
	ret=cfg_getstr(cl->value[0], s1);
	if (ret!=0) goto error;
	if (cl->token_no==2){
		ret=cfg_getstr(cl->value[1], s2);
		if (ret!=0) goto error;;
	}
	if (((int (*)(str*, str*))(callback))(s1, s2)!=0){
		LOG(L_CRIT, "ERROR: cfg: error adding peer\n");
		ret=CFG_RUN_ERR;
		goto error;
	}
	
	ret=CFG_OK;
error:
	if (s1) shm_free(s1);
	if (s2) shm_free(s2);
	return ret;
error_mem:
	LOG(L_CRIT, "ERROR: cfg: memory allocation error\n");
	ret=CFG_MEM_ERR;
	goto error;
}



/* sets a module parameter */
int cfg_set_mod_param(struct cfg_line* cl, void* unused)
{
	int ret;
	str mod_name;
	str param_name;
	str value;
	int int_val;
	struct module_param* p;
		
	ret=CFG_OK;
	if ((cl->token_no<3)){ /* mod name param_name value */
		LOG(L_CRIT, "ERROR: cfg: invalid number of "
					"parameters for peer\n");
		ret=CFG_PARAM_ERR;
		goto error;
	}
	
	ret=cfg_getstr(cl->value[0], &mod_name);
	if (ret!=0) goto error;
	ret=cfg_getstr(cl->value[1], &param_name);
	if (ret!=0) goto error;
	p=get_module_param(mod_name.s, param_name.s);
	if (p==0) goto error_noparam;
	switch(p->type){
		case INT_TYPE:
			ret=cfg_getint(cl->value[2], &int_val);
			if (ret!=0) goto error_value;
			*((int*)p->pvalue)=int_val;
			break;
		case STR_TYPE:
			ret=cfg_getstr(cl->value[2], &value);
			if (ret!=0) goto error_value;
			*(char**)(p->pvalue)=value.s;
			break;
		default:
			LOG(L_CRIT, "BUG: cfg: unknown param type %d\n", p->type);
			ret=CFG_PARAM_ERR;
	}
	
error:
	return ret;
error_value:
	LOG(L_ERR, "ERROR: cfg: bad value: %s\n", cl->value[3]);
	ret=CFG_PARAM_ERR;
	goto error;
error_noparam:
	LOG(L_ERR, "ERROR: cfg: no param <%s> found for module <%s>\n",
			mod_name.s, param_name.s);
	ret=CFG_RUN_ERR;
	goto error;
}
