/*
 * $Id: trans.h,v 1.16 2003/04/22 19:58:41 andrei Exp $
 *
 * 2003-02-11 created by bogdan
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


#ifndef _AAA_DIAMETER_TRANS_H
#define _AAA_DIAMETER_TRANS_H

#include "../str.h"
#include "../dprint.h"
#include "../hash_table.h"
#include "../timer.h"
#include "../counter.h"

struct trans;

#include "peer.h"
#include "../diameter_api/session.h"


struct trans {
	struct h_link  linker;
	/* ref counter */
	atomic_cnt ref_cnt;
	/* session the request belong to - if any */
	struct session *ses;
	/* the incoming hop-by-hop-Id - used only when forwarding*/
	unsigned int orig_hopbyhopId;
	/* info - can be used for different purposes */
	unsigned int info;

	/* incoming request peer */
	struct peer *in_peer;
	/* outgoing request peer */
	struct peer *out_peer;
	/* request buffer */
	str *req;
	/* timeout timer */
	struct timer_link timeout;
	/* timeout handler */
	void (*timeout_f)(struct trans*);
};



#define TRANS_SEVER   1<<0
#define TRANS_CLIENT  1<<1


#define TR_TIMEOUT_TIMEOUT   10
extern struct timer *tr_timeout_timer;


int init_trans_manager();


void destroy_trans_manager();


struct trans* create_transaction( str *in_buf, struct peer *in_peer ,
		void (*timeout_f)(struct trans*) );


void destroy_transaction( struct trans* );


#define update_forward_transaction_from_msg( _tr_ , _msg_ ) \
	do { \
		/* remember the received hop_by_hop-Id */ \
		(_tr_)->orig_hopbyhopId = (_msg_)->hopbyhopId; \
	} while(0)


/* search a transaction into the hash table based on endtoendID and hopbyhopID
 */
inline static struct trans* transaction_lookup(struct peer *p,
							unsigned int endtoendID, unsigned int hopbyhopID)
{
	struct h_link *linker;
	struct trans  *tr;
	str           s;
	unsigned int  hash_code;

	s.s = (char*)&endtoendID;
	s.len = sizeof(endtoendID);
	hash_code = hash( &s , p->trans_table->hash_size );
	linker = cell_lookup_and_remove( p->trans_table, hash_code, hopbyhopID);
	if (linker) {
		tr = get_hlink_payload( linker, struct trans, linker );
		if (rmv_from_timer_list( &(tr->timeout), tr_timeout_timer )!=-1)
			atomic_dec( &(tr->ref_cnt) );
		return tr;
	}
	return 0;
}



#endif
