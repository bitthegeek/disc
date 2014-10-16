/*
 * $Id: session.c,v 1.28 2003/08/29 11:41:14 bogdan Exp $
 *
 * 2003-01-28  created by bogdan
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


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <netinet/in.h>
#include "../mem/shm_mem.h"
#include "../dprint.h"
#include "../str.h"
#include "../globals.h"
#include "../utils.h"
#include "../aaa_lock.h"
#include "../locking.h"
#include "../hash_table.h"
#include "../aaa_module.h"
#include "session.h"


/* session-ID manager */
struct session_manager  ses_mgr;


/* extra functions */
static struct session* create_session( unsigned short peer_id);
static void destroy_session( struct session *ses);
static void ses_timer_handler( unsigned int ticks, void* param );


#define avp2int( _avp_ ) \
	( ntohl(*((unsigned int *)(_avp_)->data.s)) )

#define SESSION_TO_DESTROY   1<<0




/*
 * builds and inits all variables/structures needed for session management
 */
int init_session_manager( unsigned int ses_hash_size,
											unsigned int nr_shared_mutexes )
{
	/* init the session manager */
	memset( &ses_mgr, 0, sizeof(ses_mgr));

	/* build and set the shared mutexes */
	ses_mgr.shared_mutexes = create_locks(nr_shared_mutexes+2);
	if (!ses_mgr.shared_mutexes) {
		LOG(L_ERR,"ERROR:init_ssession_manager: cannot create locks!!\n");
		goto error;
	}
	ses_mgr.shared_mutexes_mutex = ses_mgr.shared_mutexes + nr_shared_mutexes;
	ses_mgr.nr_shared_mutexes = nr_shared_mutexes;
	ses_mgr.shared_mutexes_counter = 0;

	/* init the monoton_sID vector as follows:  the high 32 bits of the 64-bit
	 * value are initialized to the time, and the low 32 bits are initialized
	 * to zero */
	ses_mgr.monoton_sID[0] = 0;
	ses_mgr.monoton_sID[1] = (unsigned int)time(0) ;
	/* create the mutex */
	ses_mgr.sID_mutex = ses_mgr.shared_mutexes + nr_shared_mutexes + 1;

	/* hash table */
	ses_mgr.ses_table= build_htable( 1024 );
	if (!ses_mgr.ses_table) {
		LOG(L_CRIT,"ERROR:init_session_manager: failed to build hash table\n");
		goto error;
	}

	/* timer */
	ses_mgr.ses_timer = new_timer_list();
	if (!ses_mgr.ses_timer) {
		LOG(L_CRIT,"ERROR:init_session_manager: failed to create timer"
			" list!\n");
		goto error;
	}

	/* register timer function */
	if (register_timer( ses_timer_handler , 0/*param*/, 1/*interval*/)!=1 ) {
		LOG(L_CRIT,"ERROR:init_session_manager: failed to register timer"
			" function!\n");
		goto error;
	}

	LOG(L_INFO,"INFO:init_session_manager: session manager started\n");
	return 1;
error:
	LOG(L_INFO,"INFO:init_session_manager: FAILED to start session manager\n");
	return -1;
}



/*
 * destroy all structures used in session management
 */
void shutdown_session_manager()
{
	/* destroy the shared mutexes */
	if (ses_mgr.shared_mutexes)
		destroy_locks( ses_mgr.shared_mutexes, ses_mgr.nr_shared_mutexes + 2);

	/* destroy the hash tabel */
	if (ses_mgr.ses_table)
		destroy_htable( ses_mgr.ses_table );

	/* destroy the timer */
	if (ses_mgr.ses_timer)
		destroy_timer_list( ses_mgr.ses_timer );

	LOG(L_INFO,"INFO:shutdown_session_manager: session manager stoped\n");
	return;
}



/* assignes a new shared mutex */
static inline gen_lock_t *get_shared_mutex()
{
	unsigned int index;
	lock_get( ses_mgr.shared_mutexes_mutex );
	index = (ses_mgr.shared_mutexes_counter++)&(ses_mgr.nr_shared_mutexes-1);
	lock_release( ses_mgr.shared_mutexes_mutex );
	return &(ses_mgr.shared_mutexes[index]);
}




/************************ SESSION_ID FUNCTIONS *******************************/

/*
 * increments the 64-biti value keept in monoto_sID vector
 */
static inline void inc_64biti(int *vec_64b)
{
	vec_64b[0]++;
	vec_64b[1] += (vec_64b[0]==0);
}


/*
 * Generates a new session_ID (conforming with draft-ietf-aaa-diameter-17)
 * Returns an 1 if success or -1 if error.
 * The function is thread safe
 */
static int generate_sessionID( str *sID, unsigned int end_pad_len)
{
	char *p;

	/* some checks */
	if (!sID)
		goto error;

	/* compute id's len */
	sID->len = aaa_identity.len +
		1/*;*/ + 10/*high 32 bits*/ +
		1/*;*/ + 10/*low 32 bits*/ +
		1/*;*/ + 8/*optional value*/ +
		end_pad_len;

	/* get some memory for it */
	sID->s = (char*)shm_malloc( sID->len );
	if (sID->s==0) {
		LOG(L_ERR,"ERROR:generate_sessionID: no more free memory!\n");
		goto error;
	}

	/* build the sessionID */
	p = sID->s;
	/* aaa_identity */
	memcpy( p, aaa_identity.s , aaa_identity.len);
	p += aaa_identity.len;
	*(p++) = ';';
	/* lock the mutex for accessing "sID_gen" var */
	lock_get( ses_mgr.sID_mutex );
	/* high 32 bits */
	p += int2str( ses_mgr.monoton_sID[1] , p, 10);
	*(p++) = ';';
	/* low 32 bits */
	p += int2str( ses_mgr.monoton_sID[0] , p, 10);
	/* unlock the mutex after the 64 biti value is inc */
	inc_64biti( ses_mgr.monoton_sID );
	lock_release( ses_mgr.sID_mutex );
	/* optional value*/
	*(p++) = ';';
	p += int2hexstr( rand() , p, 8);
	/* set the correct length */
	sID->len = p - sID->s;

	return 1;
error:
	return -1;
}



/* extract hash_code and label from sID
 */
int parse_sessionID( str *sID, unsigned int *hash_code, unsigned int *label)
{
	unsigned int  rang;
	unsigned int  u;
	char c;
	char *p;
	char *s;

	/* start from the end */
	s = sID->s;
	p = sID->s + sID->len - 1;
	/* decode the label */
	u = 0;
	rang = 0;
	while(p>=s && *p!='.') {
		if ((c=hexchar2int(*p))==-1)
			goto error;
		u += ((unsigned int)c)<<rang;
		rang += 4;
		p--;
	}
	if (p<=s || *(p--)!='.')
		goto error;
	if (label)
		*label = u;
	/* decode the hash_code */
	u = 0;
	rang = 0;
	while(p>=s && *p!='.') {
		if ((c=hexchar2int(*p))==-1)
			goto error;
		u += ((unsigned int)c)<<rang;
		rang += 4;
		p--;
	}
	if (p<=s || *(p--)!='.')
		goto error;
	if (hash_code)
		*hash_code = u;

#if 0
	/* firts we have to skip 3 times char ';' */
	for(i=0;i<3&&(p<end);p++)
		if (*p==';') i++;
	if ( i!=3 || (++p>=end) )
		goto error;
	/* after first dot starts the hash_code */
	for(;(p<end)&&(*p!='.');p++);
	if ( (p>=end) || (++p>=end))
		goto error;
	/* get the hash_code; it ends with a dot */
	for(i=0;(p<end)&&(*p!='.');p++,i++);
	if (!i || (p>=end))
		goto error;
	if (hash_code && (*hash_code = hexstr2int( p-i, i))==-1)
		goto error;
	/* get the label; it ends at of EOS */
	for(p++,i=0;(p<end);p++,i++);
	if (!i)
		goto error;
	if (label && (*label = hexstr2int( p-i, i))==-1)
		goto error;
	DBG("DEBUG: session_lookup: hash_code=%u ; label=%u\n",*hash_code,*label);
#endif

	return 1;
error:
	DBG("DEBUG:parse_sessionID: sessionID [%.*s] is not generate by us! "
		"Parse error at char [%c][%d] offset %d!\n",
		(int)sID->len, sID->s,*p,*p,p-s);
	return -1;
}




/************************** SESSION FUNCTION ********************************/


static struct session* create_session( unsigned short peer_id)
{
	struct session *ses;

	/* allocates a new struct session and zero it! */
	ses = (struct session*)shm_malloc(sizeof(struct session));
	if (!ses) {
		LOG(L_ERR,"ERROR:create_session: not more free memeory!\n");
		goto error;
	}
	memset( ses, 0, sizeof(struct session));

	/* get a shared mutex for it */
	ses->mutex = get_shared_mutex();

	/* init the session */
	ses->peer_identity = peer_id;
	ses->state = AAA_IDLE_STATE;
	ses->tl.payload = ses;

	return ses;
error:
	return 0;
}



static void destroy_session( struct session *ses)
{
	if (ses) {
		if ( ses->sID.s)
			shm_free( ses->sID.s );
		if ( ses->tl.timer_list )
			rmv_from_timer_list( &(ses->tl), ses_mgr.ses_timer );
		shm_free(ses);
	}
}



static void ses_timer_handler( unsigned int ticks, void* param )
{
	struct timer_link *tl;
	struct session    *ses;

	/* get the expired sessions */
	tl = get_expired_from_timer_list( ses_mgr.ses_timer, ticks);
	while (tl) {
		ses  = (struct session*)tl->payload;
		DBG("DEBUG:ses_timeout_handler: session %p expired!\n", ses);
		tl = tl->next_tl;
		/* run the timeout handler for this session */
		((struct module_exports*)ses->app_ref)->
			mod_tout( SESSION_TIMEOUT_EVENT, &(ses->sID), ses->context);
	}
}



int session_state_machine( struct session *ses, enum AAA_EVENTS event,
															AAAMessage *msg)
{
	static char *err_msg[]= {
		"no error",
		"event - state mismatch",
		"unknown type event",
		"unknown peer_identity type",
		"internal error"
	};
	int error_code=0;
	int pending;

	DBG("DEBUG:session_state_machine: session %p, state = %d, event=%d\n",
		ses,ses->state,event);

	switch(ses->peer_identity) {
		case AAA_CLIENT:
			/* I am server, I am all the time statefull ;-) */
			break;

		case AAA_SERVER_STATELESS:
			/* I am client to a stateless server */
			switch( event ) {
				case AAA_SENDING_AuthR:
					/* an auth request has to be sent */
					lock_get( ses->mutex );
					switch(ses->state) {
						case AAA_IDLE_STATE:
						case AAA_OPEN_STATE:
							ses->prev_state = ses->state;
							ses->state = AAA_PENDING_STATE;
							lock_release( ses->mutex );
							break;
						default:
							lock_release( ses->mutex );
							error_code = 1;
							goto error;
					}
					break;
				case AAA_SENDING_AcctR:
					/* an accounting request has to be sent */
					lock_get( ses->mutex );
					switch(ses->state) {
						case AAA_DISCON_STATE:
						case AAA_TO_DESTROY_STATE:
							lock_release( ses->mutex );
							error_code = 1;
							goto error;
						default:
							ses->pending_accts++;
							lock_release( ses->mutex );
							break;
					}
					break;
				case AAA_AuthA_RECEIVED:
					/* an auth answer was received */
					lock_get( ses->mutex );
					switch(ses->state) {
						case AAA_PENDING_STATE:
							/* check the session state advertise by server */
							if (msg->auth_ses_state && SESSION_STATE_MAINTAINED
							==avp2int(msg->auth_ses_state) )
								ses->peer_identity = AAA_SERVER_STATEFULL;
							/* new state */
							if ( avp2int(msg->res_code)== AAA_SUCCESS)
								ses->state = AAA_OPEN_STATE;
							else
								ses->state = ses->prev_state;
							lock_release( ses->mutex );
							break;
						case AAA_TO_DESTROY_STATE:
							LOG(L_INFO,"INFO:session_state_machine: response "
								"received for a terminated session \n");
							if (ses->prev_state!=AAA_PENDING_STATE) {
								LOG(L_ERR,"ERROR:session_state_machine: auth. "
									"resp. received without sending req.!!\n");
								lock_release( ses->mutex );
								error_code = 1;
								goto error;
							}
							ses->prev_state = AAA_DISCON_STATE;
							pending = ses->pending_accts;
							lock_release( ses->mutex );
							if (pending==0) {
								LOG(L_INFO,"INFO:session_state_machine: "
									"destroing session \n");
								destroy_session( ses );
								goto abort;
							}
							break;
						default:
							lock_release( ses->mutex );
							error_code = 1;
							goto error;
					}
					break;
				case AAA_AcctA_RECEIVED:
					/* an accounting answer was received - we should accept
					 * the acct reply in all state - just check if a req was
					 * really sent */
					lock_get( ses->mutex );
					if (ses->pending_accts==0) {
						LOG(L_ERR,"ERROR:session_state_machine: more acct. "
							"responses received than sent acct. requests\n");
						lock_release( ses->mutex );
						error_code = 1;
						goto error;
					}
					ses->pending_accts--;
					if (ses->state==AAA_TO_DESTROY_STATE) {
						LOG(L_INFO,"INFO:session_state_machine: acct. "
							"response received for a terminated session\n");
						pending = ses->pending_accts +
							1*(ses->prev_state==AAA_PENDING_STATE);
						lock_release( ses->mutex );
						if ( pending==0 ) {
							LOG(L_INFO,"INFO:session_state_machine: "
								"destroing session \n");
							destroy_session( ses );
							goto abort;
						}
						break;
					}
					lock_release( ses->mutex );
					break;
				case AAA_SESSION_REQ_TIMEOUT:
					/* a session request gave timeout waiting for answer */
				case AAA_SEND_FAILED:
					/* a send operation that was already been registered
					 * into the session failed */
					switch (msg->commandCode) {
						case 271: /* accouting request */
							lock_get( ses->mutex );
							ses->pending_accts--;
							if (ses->state==AAA_TO_DESTROY_STATE) {
								LOG(L_INFO,"INFO:session_state_machine: acct. "
								"req. TO/failed for a terminated session\n");
								pending = ses->pending_accts +
									1*(ses->prev_state==AAA_PENDING_STATE);
								lock_release( ses->mutex );
								if ( pending==0 ) {
									LOG(L_INFO,"INFO:session_state_machine: "
										"destroing session \n");
									destroy_session( ses );
									goto abort;
								}
								break;
							}
							lock_release( ses->mutex );
							break;
						default: /* auth. request */
							lock_get( ses->mutex );
							switch(ses->state) {
								case AAA_PENDING_STATE:
									ses->state = ses->prev_state;
									lock_release( ses->mutex );
									break;
								case AAA_TO_DESTROY_STATE:
									LOG(L_INFO,"INFO:session_state_machine: "
										"timeout/failure received for a "
										"terminated session\n");
									ses->prev_state = AAA_DISCON_STATE;
									pending = ses->pending_accts;
									lock_release( ses->mutex );
									if ( pending==0 ) {
										LOG(L_INFO,"INFO:session_state_machine"
											": destroing session \n");
										destroy_session( ses );
										goto abort;
									}
									break;
								default:
									lock_release( ses->mutex );
									error_code = 1;
									goto error;
							}
						
					}
					break;
				default:
					error_code = 2;
					goto error;
			}
			break;

		case AAA_SERVER_STATEFULL:
			/* I am client to a statefull server */
			LOG(L_ERR,"ERROR: UNIMPLEMENTED - client for a "
				"statefull server\n");
			break;
#if 0
			switch( event ) {
				case AAA_SEND_AR:
					/* an re-auth request was sent */
					switch(ses->state) {
						case AAA_OPEN_STATE:
							/* update all the needed information from AR */
							//TO DO
							ses->state = AAA_OPEN_STATE;
							break;
						default:
							error_code = 1;
							goto error;
					}
					break;
				case AAA_AA_RECEIVED:
					/* a re-auth answer was received */
					switch(ses->state) {
						case AAA_OPEN_STATE:
							/* provide service or , if failed, disconnnect */
							//TO DO
							break;
						default:
							error_code = 1;
							goto error;
					}
					break;
				case AAA_SESSION_TIMEOUT:
					/* session timeout was generated */
					switch(ses->state) {
						case AAA_OPEN_STATE:
							/* send STR */
							//TO DO
							ses->state = AAA_DISCON_STATE;
							break;
						default:
							error_code = 1;
							goto error;
					}
					break;
				case AAA_ASR_RECEIVED:
					/* abort session request received */
					switch(ses->state) {
						case AAA_OPEN_STATE:
							/* send ASA and STR */
							// if user wants ????
							// TO DO 
							ses->state = AAA_DISCON_STATE;
							break;
						case AAA_DISCON_STATE:
							/* send ASA */
							// TO DO 
							ses->state = AAA_DISCON_STATE;
							break;
						default:
							error_code = 1;
							goto error;
					}
					break;
				case AAA_STA_RECEIVED:
					/* session terminate answer received  */
					switch(ses->state) {
						case AAA_DISCON_STATE:
							/* disconnect user/device */
							// destroy session ??
							// TO DO
							ses->state = AAA_IDLE_STATE;
							break;
						default:
							error_code = 1;
							goto error;
					}
					break;
				default:
					error_code = 2;
					goto error;
			}
			break;
#endif
		default:
			error_code = 3;
			goto error;
	}
	DBG("DEBUG:session_state_machine: session %p, new state = %d\n",
		ses,ses->state);

	return 1;
abort:
	return 0;
error:
	LOG(L_ERR,"ERROR:session_state_machine: %s : session=%p, peer_identity=%d,"
		" state=%d, event=%d\n",err_msg[error_code],
		ses,ses->peer_identity,ses->state,event);
	return -1;
}




/****************************** API FUNCTIONS ********************************/


AAAReturnCode  AAAStartSession( 
				AAASessionId **sessionId,
				AAAApplicationRef appReference, 
				void *context
				)
{
	struct session  *session;
	char            *p;

	if (!sessionId || !appReference) {
		LOG(L_ERR,"ERROR:AAAStartSession: invalid params received!\n");
		goto error;
	}

	/* build a new session structure */
	session = create_session( AAA_SERVER );
	if (!session)
		goto error;

	/* link info into the session */
	session->context = context;
	session->app_ref  = appReference;

	/* generates a new session-ID - the extra pad is used to append to 
	 * session-ID the hash-code and label of the session ".XXXXXXXX.XXXXXXXX"*/
	if (generate_sessionID( &(session->sID), 2*9 )!=1)
		goto error;

	/* compute the hash code; !!!IMPORTANT!!! -> this must happen before 
	 * inserting the session into the hash table, otherwise, the entry to
	 * insert on , will not be known */
	session->linker.hash_code = hash( &(session->sID),
		ses_mgr.ses_table->hash_size);

	/* insert the session into the hash table */
	add_cell_to_htable( ses_mgr.ses_table, (struct h_link*)session);

	/* now we have both the hash_code and the label of the session -> append
	 * them to the and of session ID */
	p = session->sID.s + session->sID.len;
	*(p++) = '.';
	p += int2hexstr( session->linker.hash_code, p, 8);
	*(p++) = '.';
	p += int2hexstr( session->linker.label, p, 8);
	session->sID.len = p - session->sID.s;

	/* return the session-ID */
	*sessionId = &(session->sID);

	return AAA_ERR_SUCCESS;
error:
	*sessionId = 0;
	return AAA_ERR_NOMEM;
}




AAAReturnCode AAASessionTimerStart( AAASessionId *sessionId , unsigned int to)
{
	struct session *ses;
	ses = sId2session( sessionId );

	/* add the session into the timer list */
	insert_into_timer_list( &(ses->tl), ses_mgr.ses_timer,
		get_ticks()+to);

	return AAA_ERR_SUCCESS;
}




AAAReturnCode AAASessionTimerStop( AAASessionId *sessionId )
{
	struct session *ses;
	ses = sId2session( sessionId );

	/* remove the sessoin from timer list */
	rmv_from_timer_list( &(ses->tl), ses_mgr.ses_timer );

	return AAA_ERR_SUCCESS;
}




AAAReturnCode AAAEndSession( AAASessionId *sessionId )
{
	struct session *ses;
	ses = sId2session( sessionId );

	if (ses->peer_identity==AAA_SERVER_STATEFULL) {
		/* server is statefull -> maybe I have to send a STR */
		// TO DO !!!!!!!!!
	} else {
		/* for stateless servers -> just remove it */
		remove_cell_from_htable( ses_mgr.ses_table, &(ses->linker) );
		lock_get( ses->mutex );
		if (ses->state==AAA_PENDING_STATE || ses->pending_accts) {
			LOG(L_INFO,"INFO:AAAEndSession: removing a session that waits"
				" for some answers -> destroy postponded\n");
			ses->prev_state = ses->state;
			ses->state = AAA_TO_DESTROY_STATE;
			lock_release( ses->mutex );
		} else {
			lock_release( ses->mutex );
			LOG(L_INFO,"INFO:AAAEndSession: destroing session\n");
			destroy_session( ses );
		}
	}

	return AAA_ERR_SUCCESS;
}




