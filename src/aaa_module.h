/*
 * $Id: aaa_module.h,v 1.16 2003/09/11 22:24:09 cco Exp $
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

/* History:
 * --------
 *  2003-03-28  created by andrei
 *  2003-04-13  added module parameters (andrei)
 */

#ifndef aaa_module_h
#define aaa_module_h

#include "diameter_api/diameter_api.h"

#define DOES_AUTH        (1<<0)
#define DOES_ACCT        (1<<1)
#define RUN_ON_REPLIES   (1<<2)

extern str module_path;

enum param_types { INT_TYPE, STR_TYPE };

struct module_param{
	char* name;
	enum param_types type;
	void* pvalue;
};

struct module_exports{
	char* name; /* module name, must be unique */
	unsigned int mod_type; /* module's type - server or client */
	unsigned int app_id; /* application id*/
	unsigned int flags; /* flags */
	struct module_param* params;/* module parameters, null terminated array*/
	
	int (*mod_init)();   /* module initialization function */
	void (*mod_destroy)(); /* called on exit */
	
	int (*mod_msg)(AAAMessage*, void*); /* called for each message */
	int (*mod_tout)(int, AAASessionId*, void*);  /* called for all timeout */
};



struct aaa_module{
	char* path;
	void* handle;
	struct module_exports* exports;
	int is_init;
	struct aaa_module* next;
};


extern struct aaa_module* modules; /* global module list */


int init_module_loading();
int load_module(char*);
int init_modules();
void destroy_modules();
void unload_modules();
struct aaa_module* find_module(unsigned int app_id);
struct module_param* get_module_param(char* module, char* param_name); 

#endif /*aaa_module_h*/
