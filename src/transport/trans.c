/*
 * $Id: trans.c,v 1.13 2003/04/22 19:58:41 andrei Exp $
 *
 * 2003-02-11  created by bogdan
 * 2003-03-12  converted to shm_malloc/shm_free (andrei)
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


#include <stdlib.h>
#include <string.h>
#include "../mem/shm_mem.h"
#include "../dprint.h"
#include "../hash_table.h"
#include "../diameter_api/diameter_api.h"
#include "trans.h"



struct timer *tr_timeout_timer=0;



void timeout_handler(unsigned int ticks, void* param);


int init_trans_manager()
{
	/* build the timer list for transaction timeout */
	tr_timeout_timer = new_timer_list();
	if (!tr_timeout_timer) {
		LOG(L_ERR,"ERROR:init_trans_manager: cannot build timer\n");
		goto error;
	}

	/* register timer function */
	if (register_timer( timeout_handler, tr_timeout_timer, 1)==-1) {
		LOG(L_ERR,"ERROR:init_trans_manager: cannot register time handler\n");
		goto error;	}

	LOG(L_INFO,"INFO:init_trans_manager: transaction manager started!\n");
	return 1;
error:
	return -1;
}



void destroy_trans_manager()
{
	if (tr_timeout_timer)
		destroy_timer_list( tr_timeout_timer );
	LOG(L_INFO,"INFO:init_trans_manager: transaction manager stoped!\n");
}



struct trans* create_transaction( str *buf, struct peer *in_peer,
										void (*timeout_f)(struct trans*) )
{
	struct trans *t;

	t = (struct trans*)shm_malloc(sizeof(struct trans));
	if (!t) {
		LOG(L_ERR,"ERROR:create_in_transaction: no more free memory!\n");
		goto error;
	}
	memset(t,0,sizeof(struct trans));

	/* init the timer_link for timeout */
	t->timeout.payload = t;
	/* link the request buffer */
	t->req = buf;
	/* link the incoming peer */
	t->in_peer = in_peer;

	/* only one ref from the thread that build the transaction */
	atomic_set( &t->ref_cnt, 1);

	/* timeout handler */
	t->timeout_f = timeout_f;

	return t;
error:
	return 0;
}




void destroy_transaction( struct trans *tr )
{
	if (!tr) {
		LOG(L_ERR,"ERROR:destroy_transaction: null parameter received!\n");
		return;
	}

	if ( atomic_dec_and_test( &tr->ref_cnt ) ) {
		/* transaction is no longer referenceted by anyone -> it's save to
		 * destroy it */
		shm_free(tr);
	} else {
		LOG(L_WARN,"WARNING:destroy_transaction: transaction still "
			"referenceted - destroy aborted!\n");
	}
}




void timeout_handler(unsigned int ticks, void* param)
{
	struct timer_link *tl;
	struct trans *tr;

	/* TIMEOUT TIMER LIST */
	/* get the expired transactions */
	tl = get_expired_from_timer_list( tr_timeout_timer, ticks);
	while (tl) {
		tr  = (struct trans*)tl->payload;
		DBG("DEBUG:timeout_handler: transaction %p expired!\n",tr);
		tl = tl->next_tl;
		/* remove the transaction from hash table */
		if (remove_cell_from_htable(tr->out_peer->trans_table,
		&(tr->linker))==-1) {
			LOG(L_NOTICE,"NOTICE:trans_timeout_handler: race-condition "
				"between trasaction timeout and incoming reply -> timeout "
				"drop\n");
		} else {
			/* I successfuly removed the transaction from both timer list and
			 * hash table -> I will inherit the reference from timer list and
			 * loose the one from hash_table */
			atomic_dec( &tr->ref_cnt );
			/* process the transaction */
			if (tr->timeout_f)
				tr->timeout_f( tr );
		}
		/* destroy the transaction */
		destroy_transaction( tr );
	}


}
