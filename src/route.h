/* 
 * $Id: route.h,v 1.7 2003/04/22 19:58:41 andrei Exp $
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
 *
 * History:
 * --------
 *  2003-04-08  created by andrei
 */


#ifndef route_h
#define route_h

#include "str.h"
#include "aaa_parse_uri.h"
#include "transport/peer.h"
#include "diameter_msg/diameter_msg.h"


struct peer_entry{
	struct aaa_uri uri;
	str full_uri;
	str alias;
	struct peer* peer; /* pointer to internal peer structure*/
	struct peer_entry *next;
};

extern struct peer_entry* cfg_peer_lst;


struct peer_entry_list{
	struct peer_entry* pe;
	struct peer_entry_list *next;
};

struct route_entry{
	str realm;
	struct peer_entry_list* peer_l; /* peer list */
	struct route_entry* next;
};

extern struct route_entry* route_lst;


int add_cfg_peer(str* uri, str* alias);
int add_route(str* realm, str* dst);
struct peer_entry_list* route_dest(str* realm);
int do_route(AAAMessage *msg, struct peer* in_peer);

/* destroy functions, call them to free the memory */
void destroy_cfg_peer_lst();
void destroy_route_lst();




#endif
