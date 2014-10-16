/*
 * $Id: globals.h,v 1.10 2003/04/22 19:58:41 andrei Exp $
 *
 * 2003-02-03  created by bogdan
 * 2003-04-16  added lots of startup config. vars (andrei)
 *
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



#ifndef _AAA_DIAMETER_GLOBAL_H
#define _AAA_DIAMETER_GLOBAL_H

#include "str.h"

/* for shm_mem */
extern int memlog;
extern unsigned int shm_mem_size;

/* listening port for aaa connections */
extern unsigned int listen_port;

/* if 1 disable ipv6 */
extern int disable_ipv6;

extern int log_stderr;

extern int dont_fork;
extern char* chroot_dir;
extern char* working_dir;
extern char* user;
extern char* group;
extern int uid;
extern int gid;
extern char* pid_file;

/* aaa identity */

/* aaa_identity string */
extern str aaa_identity;

/* the served realm */
extern str aaa_realm;

/* the FQDN of the machin I'm running on */
extern str aaa_fqdn;

/* product name */
extern str product_name;

/* vendor-id */
extern unsigned int vendor_id;

/* if the server does or not relaying */
extern unsigned int do_relay;

/* my status - client, server, statefull server */
extern unsigned int my_aaa_status;

/* number of working threads */
extern unsigned int worker_threads;

/* number of peer threads */
extern unsigned int reader_threads;

#endif

