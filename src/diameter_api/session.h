/*
 * $Id: session.h,v 1.19 2003/08/25 14:52:02 bogdan Exp $
 *
 * 2003-01-28 created by bogdan
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



#ifndef _AAA_DIAMETER_SESSION_H
#define _AAA_DIAMETER_SESSION_H


#include "diameter_api.h"
#include "../aaa_lock.h"
#include "../hash_table.h"
#include "../locking.h"
#include "../timer.h"


#define SESSION_STATE_MAINTAINED      0
#define SESSION_NO_STATE_MAINTAINED   1

#define sId2session( _sId_ ) \
	((struct session*)((char *)(_sId_) - \
	(unsigned long)(&((struct session*)0)->sID)))



/* all possible states of the session state machine */
enum {
	AAA_IDLE_STATE,
	AAA_PENDING_STATE,
	AAA_OPEN_STATE,
	AAA_DISCON_STATE,
	AAA_TO_DESTROY_STATE,
};

/* all possible events for the session state machines */
enum AAA_EVENTS {
	AAA_AcctR_RECEIVED,
	AAA_AcctA_RECEIVED,
	AAA_SENDING_AcctR,
	AAA_SENDING_AcctA,
	AAA_AuthA_RECEIVED,
	AAA_AuthR_RECEIVED,
	AAA_SENDING_AuthR,
	AAA_SENDING_AuthA,
	AAA_ASR_RECEIVED,
	AAA_ASA_RECEIVED,
	AAA_SENDING_ASR,
	AAA_SENDING_ASA,
	AAA_RAR_RECEIVED,
	AAA_RAA_RECEIVED,
	AAA_SENDING_RAR,
	AAA_SENDING_RAA,
	AAA_STR_RECEIVED,
	AAA_STA_RECEIVED,
	AAA_SENDING_STR,
	AAA_SENDING_STA,
	AAA_SESSION_REQ_TIMEOUT,
	AAA_SEND_FAILED,
};



/*
 * all session manager's info packed
 */
struct session_manager {
	/* SESSION ID */
	/* vector used to store the monotonically increasing 64-bit value used
	 * in session ID generation */
	unsigned int monoton_sID[2];
	/* mutex */
	gen_lock_t *sID_mutex;

	/* SESSIONS */
	/* hash_table */
	struct h_table *ses_table;
	/* timer */
	struct timer *ses_timer;

	/* SHARED MUTEXES */
	/* mutexes that are distributed to the sessions */
	gen_lock_t *shared_mutexes;
	/* mutex for protecting disribution of shared mutexes :-)) */
	gen_lock_t *shared_mutexes_mutex;
	/* number of shared mutexes */
	unsigned int nr_shared_mutexes;
	/* index showing the next shared mutex to be used */
	unsigned int shared_mutexes_counter;
};



/*
 * encapsulates a everything about a AAA session
 */
struct session {
	/* linker into hash table */
	struct h_link  linker;
	/* linmker into tiler list */
	struct timer_link tl;
	/* mutex */
	gen_lock_t *mutex;
	/* AAA info */
	unsigned short peer_identity;        /* is it a client, server ....? */
	str sID;                             /* session-ID as string */
	/* context  */
	AAAApplicationRef app_ref;
	void  *context;
	/* session status */
	unsigned int state;
	unsigned int prev_state;
	/* sent acct. reqs. without resposne/timeout */
	unsigned int pending_accts;
};


/* session-IDs manager */
extern struct session_manager  ses_mgr;



/* builds and init all variables/structures needed for session management
 */
int init_session_manager( unsigned int ses_hash_size,
											unsigned int nr_shared_mutexes);


/*
 */
void shutdown_session_manager();


/* parse a session-Id and retrive the hash-code and label from it
 */
int parse_sessionID( str* ,unsigned int* ,unsigned int* );


/*
 */
int session_state_machine( struct session* , enum AAA_EVENTS event,
															AAAMessage *msg);


/* search into hash table a session, based on session-Id
 */
inline static struct session* session_lookup(struct h_table *table,
															AAASessionId *sID)
{
	struct h_link  *linker;
	unsigned int   hash_code;
	unsigned int   label;

	if (parse_sessionID(  sID, &hash_code, &label)!=1)
		return 0;
	linker = cell_lookup( table, hash_code, label);
	if (linker)
		return ((struct session*)((char *)(linker) -
			(unsigned long)(&((struct session*)0)->linker))) ;
	else
		return 0;
}


#endif
