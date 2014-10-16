/*
 * $Id: aaa_module.c,v 1.15 2003/04/22 19:58:41 andrei Exp $
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
 *  2003-03-27  created by andrei
 *  2003-04-02  LTDL_SET_PRELOADED_SYMBOLS added (andrei)
 *  2003-04-13  module parameters added (andrei)
 */

#include <ltdl.h>

#include "config.h"
#include "dprint.h"
#include "mem/shm_mem.h"
#include "globals.h"
#include "aaa_module.h"


struct aaa_module* modules=0;
str module_path={MODULE_SEARCH_PATH, sizeof(MODULE_SEARCH_PATH)};


int init_module_loading()
{
	int ret;
	static int initialised=0;
	
	if (initialised){
		LOG(L_CRIT, "BUG: init_module_loading: already intialised\n");
		return -1;
	}
	/* make sure preloaded modules are initialised */
	/*LTDL_SET_PRELOADED_SYMBOLS();*/
	ret=lt_dlinit();
	
	if (ret){
		LOG(L_CRIT, "ERROR: init_module_loading: lt_dlinit failed: %s\n",
				lt_dlerror());
		goto error;
	}
	
	ret=lt_dlsetsearchpath(MODULE_SEARCH_PATH);
	if (ret){
		LOG(L_CRIT, "ERROR: init_module_loading: lt_dlsetsearchpath "
					"failed: %s\n",
				lt_dlerror());
		goto error;
	}
	initialised++;
	return ret;
error:
	lt_dlexit();
	return -1;
}



int load_module(char* name)
{
	int ret;
	lt_dlhandle handle;
	struct module_exports* e;
	char* error_msg;
	struct aaa_module* mod;
	struct aaa_module** m;
	
	ret=0;
	mod=0;
	handle=0;
	e=0;
	
	handle=lt_dlopenext(name);
	if (handle==0){
		LOG(L_CRIT, "ERROR: load_module: failed to load <%s> in <%s>: %s\n",
					name, MODULE_SEARCH_PATH, lt_dlerror());
		ret=-1;
		goto error;
	}
	/* get module struct */
	e=(struct module_exports*)lt_dlsym(handle, "exports");
	error_msg=(char*)lt_dlerror();
	if (e==0){
		LOG(L_CRIT, "ERROR: load_module: symbol not found <%s>: %s\n",
				name, error_msg);
		ret=-1;
		goto error;
	}
	/* sanity checks */
	if (e->mod_type!=AAA_CLIENT && e->mod_type!=AAA_SERVER) {
		LOG(L_CRIT, "ERROR: load_module: module \"%s\" has an unknown "
			"type (%d) - try AAA_SERVER or AAA_CLIENT\n",e->name, e->mod_type);
		ret=-1;
		goto error;
	}
	if (e->mod_type!=my_aaa_status) {
		LOG(L_CRIT, "ERROR: load_module: module \"%s\" has a different "
			"type (%d) then the core (%d)\n",e->name,e->app_id,my_aaa_status);
		ret=-1;
		goto error;
	}
	if (e->app_id==AAA_APP_RELAY) {
		LOG(L_CRIT, "ERROR: load_module: module \"%s\" advertises "
			"an app_id that's reserved %x (relay)\n",e->name, e->app_id);
		ret=-1;
		goto error;
	}
	if ((e->flags|DOES_AUTH|DOES_AUTH)==0) {
		LOG(L_CRIT, "ERROR: load_module: module \"%s\" does not support "
			"authentication or accounting for app_id %d\n",e->name, e->app_id);
		ret=-1;
		goto error;
	}
	if (e->mod_msg==0 ) {
		LOG(L_CRIT, "ERROR: load_module: module \"%s\" exports a NULL "
			"function for mod_msg\n",e->name);
		ret=-1;
	}
	/* link it in the module list - TODO*/
	mod=shm_malloc(sizeof(struct aaa_module));
	if (mod==0){
		LOG(L_CRIT, "ERROR: load_module: memory allocation failure\n");
		ret=-2;
		goto error;
	}
	mod->path=name;
	mod->handle=handle;
	mod->exports=e;
	mod->is_init=0;
	mod->next=0;
	for (m=&modules; *m; m=&(*m)->next){
		if ((*m)->handle==mod->handle){
			LOG(L_WARN, "WARNING: load_module: attempting to load the same"
					" module twice (%s)\n", mod->path);
			shm_free(mod);
			goto skip;
		}
		if (strcmp((*m)->exports->name, e->name)==0){
			LOG(L_CRIT, "ERROR: load_module: module name collision for %s\n",
					e->name);
			ret=-3;
			goto error;
		}
		if ((*m)->exports->app_id==e->app_id){
			LOG(L_CRIT, "ERROR: load_module: 2 modules with the same "
					"app_id(%u): %s, %s\n",
					e->app_id, (*m)->exports->name, e->name);
			ret=-4;
			goto error;
		}
	}
	*m=mod;
skip:
	return ret;
	
error:
	if(handle) lt_dlclose(handle);
	if(mod) shm_free(mod);
	return ret;
}


/*
 * Initializes all the loaded modules */
int init_modules()
{
	struct aaa_module* a;
	
	for (a=modules; a; a=a->next){
		if((a->exports)&&(a->exports->mod_init)) {
			if(a->exports->mod_init()!=0){
				LOG(L_CRIT, "ERROR: init_modules: module %s failed to "
						"initialize\n", a->exports->name);
				return -1;
			} else {
				a->is_init = 1;
			}
		}
	}
	return 0;
}


/* destroys all the modules */
void destroy_modules()
{
	struct aaa_module* a;
	struct aaa_module* b;
	
	/* first call module->destroy */
	for (a=modules; a; a=a->next ) {
		if ((a->exports)&&(a->exports->mod_destroy)&&(a->is_init))
			a->exports->mod_destroy();
	}
	for (a=modules; a;){
		b = a->next;
		if (a->path) shm_free(a->path);
		shm_free( a );
		a = b;
	}
	modules=0;
}



/* unload modules - WARNING: do not call this if you use anything from a
 * module; do not call qm_status after this if you still have non-deallocated
 * mem. from the modules */
void unload_modules()
{
	if (lt_dlexit()!=0){ /* unload modules */
		LOG(L_CRIT, "ERROR: destroy_modules: lt_dlexit: %s\n", lt_dlerror());
	};
}


/* finds a module, given an appid */
struct aaa_module* find_module(unsigned int app_id)
{
	struct aaa_module* a;
	
	for (a=modules; a; a=a->next)
		if ((a->exports)&&(a->exports->app_id==app_id)) return a;
	return 0;
}


struct aaa_module* find_module_by_name(char* name)
{
	struct aaa_module* a;
	
	for (a=modules; a; a=a->next)
		if ((a->exports)&&(strcmp(a->exports->name, name)==0)) return a;
	return 0;
}


struct module_param* get_module_param(char* module, char* param_name)
{
	struct aaa_module* a;
	struct module_param* p;
	
	if ((a=find_module_by_name(module))==0)  goto no_module_found;
	for (p=a->exports->params;p&&(p->name);p++){
		if (strcmp(param_name, p->name)==0)
			return p;
	}
no_module_found:
	return 0;
}
