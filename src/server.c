/*
 * $Id: server.c,v 1.12 2003/04/22 19:58:41 andrei Exp $
 *
 * 2003-04-08 created by bogdan
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




#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>

#include "mem/shm_mem.h"
#include "dprint.h"
#include "timer.h"
#include "globals.h"
#include "list.h"
#include "msg_queue.h"
#include "aaa_module.h"
#include "diameter_api/diameter_api.h"
#include "transport/trans.h"
#include "transport/peer.h"
#include "route.h"




int server_send_local_req( AAAMessage *msg, struct trans *tr)
{
	LOG(L_ERR,"BUG:server_send_local_req: UNIMPLEMETED - "
		"we shouldn't get here!!\n");
	return -1;
}



static void send_error_reply(AAAMessage *msg, unsigned int response_code)
{
	AAAMessage *ans;

	DBG("DEBUG:send_error_reply: sending error answer response %d\n",
			response_code);
	ans = AAANewMessage( msg->commandCode, msg->applicationId,
		&(msg->sessionId->data), msg );
	if (!ans) {
		LOG(L_ERR,"ERROR:send_error_reply: cannot create error answer"
			" back to client!\n");
	} else {
		if (AAASetMessageResultCode( ans, response_code)==AAA_ERR_SUCCESS ) {
			if (AAASendMessage( ans )!=AAA_ERR_SUCCESS )
				LOG(L_ERR,"ERROR:send_error_reply: unable to send "
					"answer back to client!\n");
		}else{
			LOG(L_ERR,"ERROR:send_error_reply: unable to set the "
				"error result code into the answer\n");
		}
		AAAFreeMessage( &ans );
	}
}



static inline void run_module( AAAMessage *msg , struct aaa_module *mod)
{
	DBG("******* server received  local request \n");
	/* if the server is statefull, I have to call here the session
	 * state machine for the incoming request TO DO */
	msg->sId = &(msg->sessionId->data);
	/*run the handler for this module */
	mod->exports->mod_msg( msg, 0);
}



static inline int forward_request( AAAMessage *msg, struct peer *in_p,
														struct peer *out_p)
{
	struct trans *tr;

	DBG("********* forwarding request to peer %p [%.*s]\n",
		out_p, out_p->aaa_identity.len, out_p->aaa_identity.s );
	
	/* forward the request -> build a transaction for it */
	tr = create_transaction( &(msg->buf), in_p, 0);
	if (!tr)
		return -1;
	update_forward_transaction_from_msg( tr , msg );
	/* send it out */
	if ( send_req_to_peer( tr, out_p)==-1) {
		LOG(L_ERR,"ERROR:forwar_request: unable to forward request\n");
		destroy_transaction( tr );
		return -1;
	}
	/* success */
	return 1;
}



static inline void process_incoming_request(AAAMessage *msg,struct peer *in_p )
{
	struct peer        *out_p;
	struct aaa_module  *mod;

	/* is Dest-Host present? */
	if (msg->dest_host) {
		/* dest-host avp present */
		if (!msg->dest_realm) {
			/* has dest-host but no dest-realm -> bogus msg */
			send_error_reply( msg, AAA_MISSING_AVP);
			return;
		}
		/* check the dest-host */
		if (msg->dest_host->data.len==aaa_identity.len &&
		!strncasecmp(msg->dest_host->data.s,aaa_identity.s,aaa_identity.len)) {
			/* I'm the destination host -> do I support the application ? */
			if ( (mod=find_module(msg->applicationId))!=0 ) {
				/* process the request localy */
				run_module( msg, mod);
			} else {
				/* no module to deal with this kind of application */
				send_error_reply( msg, AAA_APPLICATION_UNSUPPORTED);
			}
			return;
		} else {
			/* I'm not the destination host -> am I peer with the dest-host? */
			out_p = lookup_peer_by_identity( &(msg->dest_host->data) );
			if (out_p) {
				/* the destination host is one of my peers */
				if (forward_request( msg, in_p, out_p)==-1)
					send_error_reply( msg, AAA_UNABLE_TO_DELIVER);
				return;
			}
			/* do routing based on dest realm */
		}
	} else {
		if (!msg->dest_realm) {
			/* no dest-host and no dest-realm -> it's local */
			if ( (mod=find_module(msg->applicationId))!=0 ) {
				/* process the request localy */
				run_module( msg, mod);
			} else {
				/* no module to deal with this kind of application */
				send_error_reply( msg, AAA_APPLICATION_UNSUPPORTED);
			}
			return;
		}
		/* do routing based on dest realm */
	}

	DBG("*** doing routing based on realms : mine=[%.*s] req=[%.*s]\n",
			aaa_realm.len,aaa_realm.s,
			msg->dest_realm->data.len,msg->dest_realm->data.s);
	/* do routing based on destination-realm AVP */
	if (msg->dest_realm->data.len==aaa_realm.len &&
	!strncasecmp(msg->dest_realm->data.s, aaa_realm.s, aaa_realm.len) ) {
		/* I'm the destination realm */
		/* do I support the requested app_id? */
		if ( (mod=find_module(msg->applicationId))!=0) {
			/* I support the application localy */
			run_module( msg, mod);
		} else {
			DBG("********** forward inside realm\n");
			/* do I have a peer in same realm that supports this app-Id? */
			out_p = lookup_peer_by_realm_appid( &msg->dest_realm->data,
				msg->applicationId );
			if ( out_p ) {
				/* forward the request to this peer */
				if (forward_request( msg, in_p, out_p)==-1)
					send_error_reply( msg, AAA_UNABLE_TO_DELIVER);
			} else {
				send_error_reply( msg, AAA_APPLICATION_UNSUPPORTED);
			}
		}
	} else {
		/* it's not my realm -> do routing based on script */
		// TO DO
		DBG(" routing tabel  \n ");
		if (do_route(msg, in_p)<0){
			send_error_reply( msg, AAA_UNABLE_TO_DELIVER);
		}
	}
}




void *server_worker(void *attr)
{
	str               buf;
	AAAMessage        *msg;
	struct aaa_module *mod;
	struct trans      *tr;
	struct peer       *in_peer;

	while (1) {
		/* read a mesage from the queue */
		if (get_from_queue( &buf, &in_peer )==-1) {
			usleep(500);
			continue;
		}

		/* parse the message */
		msg = AAATranslateMessage( buf.s, buf.len , 1/*attach buffer*/ );
		if (!msg) {
			LOG(L_ERR,"ERROR:server_worker: dropping message!\n");
			shm_free( buf.s );
			continue;
		}

		/* process the message */
		if ( is_req(msg) ) {
			/* request*/
			msg->in_peer = in_peer;
			DBG(" ******** request received!!! \n");
			/* sanity checks */
			if (msg->commandCode==274 || msg->commandCode==258 ||
			msg->commandCode==275 ) {
				LOG(L_ERR,"ERROR:server_worker: request 274(ASR)/258(RAR)/"
					"275(275) received - unsupported -> droping!\n");
				send_error_reply( msg, AAA_COMMAND_UNSUPPORTED);
				AAAFreeMessage( &msg );
				continue;
			}
			if (!msg->sessionId || !msg->orig_host || !msg->orig_realm ||
			!msg->dest_realm) {
				LOG(L_ERR,"ERROR:server_worker: message without sessionId/"
					"OriginHost/OriginRealm/DestinationRealm AVP received "
					"-> droping!\n");
				send_error_reply( msg, AAA_MISSING_AVP);
				AAAFreeMessage( &msg );
				continue;
			}
			/* process the request */
			process_incoming_request( msg, in_peer);
		} else {
				DBG(" ******** response received!!! \n");
			/* response -> performe transaction lookup and remove it from 
			 * hash table */
			tr = transaction_lookup(in_peer,msg->endtoendId,msg->hopbyhopId);
			if (!tr) {
				LOG(L_ERR,"ERROR:server_worker: unexpected answer received "
					"(without sending any request)!\n");
				AAAFreeMessage( &msg  );
				continue;
			}
			/* process the reply */
			if (tr->in_peer) {
					DBG(" ******** doing downstream forward!!! \n");
				/* I have to forward the reply downstream */
				msg->hopbyhopId = tr->orig_hopbyhopId;
				((unsigned int*)msg->buf.s)[3] = tr->orig_hopbyhopId;
				/* is there some module interested in this reply ? */
				mod = find_module(msg->applicationId);
				if (mod && mod->exports->flags&RUN_ON_REPLIES) {
					DBG(" ******* running module (%p) for reply \n",mod);
					mod->exports->mod_msg( msg, 0);
				}
				/* send the rely */
				if (send_res_to_peer( &msg->buf, tr->in_peer)==-1) {
					LOG(L_ERR,"ERROR:server_worker: forwarding reply "
						"failed\n");
				}
			} else {
				/* look like being a reply to a local request */
				LOG(L_ERR,"BUG:server_worker: some reply to a local request "
					"received; but I don't send requests!!!!\n");
			}
			/* destroy the transaction */
			destroy_transaction( tr );
		}
		/* free the mesage (along with the buffer) */
		AAAFreeMessage( &msg  );
	}

	return 0;
}

