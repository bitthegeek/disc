/*
 * $Id: sender.c,v 1.11 2003/08/25 14:52:02 bogdan Exp $
 *
 * 2003-02-03 created by bogdan
 * 2003-03-12 converted to use shm_malloc/shm_free (andrei)
 * 2003-03-13 converted to locking.h/gen_lock_t (andrei)
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
#include <netinet/in.h>

#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>

#include "../mem/shm_mem.h"
#include "../dprint.h"
#include "../str.h"
#include "../utils.h"
#include "../locking.h"
#include "../globals.h"
#include "../aaa_module.h"
#include "../transport/peer.h"
#include "../transport/trans.h"
#include "diameter_api.h"
#include "session.h"


extern int (*send_local_request)(AAAMessage*, struct trans*);



static void ses_trans_timeout_f( struct trans *tr ) {
	static AAAMessage fake_msg;

	fake_msg.commandCode = tr->info;
	if (session_state_machine(tr->ses,AAA_SESSION_REQ_TIMEOUT,&fake_msg)==1) {
		/* run the timeout handler */
		((struct module_exports*)tr->ses->app_ref)->mod_tout(
			ANSWER_TIMEOUT_EVENT, &(tr->ses->sID), tr->ses->context);
	}
}


/****************************** API FUNCTIONS ********************************/


/* The following function sends a message to the server assigned to the
 * message by AAASetServer() */
AAAReturnCode  AAASendMessage(AAAMessage *msg)
{
	struct session    *ses;
	struct trans      *tr;
	unsigned int      event;
	int               ret;

	ses = 0;
	tr  = 0;

	/* some checks */
	if (!msg)
		goto error;

	if (msg->commandCode==257||msg->commandCode==280||msg->commandCode==282) {
		LOG(L_ERR,"ERROR:AAASendMessage: you are not ALLOWED to send this type of message (%d) -> read the draft!!!\n",msg->commandCode);
		goto error;
	}

	if (msg->commandCode==274||msg->commandCode==275||msg->commandCode==258) {
		LOG(L_ERR,"ERROR:AAASendMessage: statefull not "
			"implemented; cannot send %d!\n",msg->commandCode);
		goto error;
	}

	if ( !is_req(msg) ) {
		/* it's a response */
		if (my_aaa_status==AAA_CLIENT) {
			LOG(L_ERR,"ERROR:AAASendMessage: AAA client does not send answers!! -> read the draft!!!\n");
			goto error;
		}

		/* generate the buf from the message */
		if ( AAABuildMsgBuffer( msg )==-1 )
			goto error;

		if ( my_aaa_status!=AAA_SERVER ) {
			ses = sId2session( msg->sId );
			/* update the session state machine */
			switch (msg->commandCode) {
				case 274: /*ASA*/ event = AAA_SENDING_ASA; break;
				case 275: /*STA*/ event = AAA_SENDING_STA; break;
				case 258: /*RAA*/ event = AAA_SENDING_RAA; break;
				case 271: /*acctA*/ event = AAA_SENDING_AcctA; break;
				default:  /*authA*/ event = AAA_SENDING_AuthA; break;
			}
			if (session_state_machine( ses, event, msg)!=1)
				goto error;
		}

		/* send the reply to the request incoming peer  */
		if (send_res_to_peer( &(msg->buf), (struct peer*)msg->in_peer)==-1) {
			LOG(L_ERR,"ERROR:send_aaa_response: send returned error!\n");
			if ( my_aaa_status!=AAA_SERVER )
				session_state_machine( ses, AAA_SEND_FAILED, msg);
			goto error;
		}
	} else {
		/* it's a request */
		if (my_aaa_status!=AAA_CLIENT) {
			LOG(L_ERR,"ERROR:AAASendMessage: AAA stateless server does not "
				"send request!! -> read the draft!!!\n");
			goto error;
		}

		/* -> get its session */
		ses = sId2session( msg->sId );

		/* generate the buf from the message */
		if ( AAABuildMsgBuffer( msg )==-1 )
			goto error;

		/* build a new outgoing transaction for this request */
		if ((tr=create_transaction(&(msg->buf), 0, ses_trans_timeout_f))==0 ) {
			LOG(L_ERR,"ERROR:AAASendMesage: cannot create a new"
				" transaction!\n");
			goto error;
		}
		tr->ses = ses;
		tr->info = (unsigned int)msg->commandCode;

		/* update the session state */
		switch (msg->commandCode) {
			case 274: /*ASR*/ event = AAA_SENDING_ASR; break;
			case 275: /*STR*/ event = AAA_SENDING_STR; break;
			case 258: /*RAR*/ event = AAA_SENDING_RAR; break;
			case 271: /*acctA*/ event = AAA_SENDING_AcctR; break;
			default:  /*authA*/ event = AAA_SENDING_AuthR; break;
		}
		if (session_state_machine( ses, event, msg)!=1)
			goto error;

		/* route and send the message */
		ret = send_local_request( msg, tr );
		if (ret==-1) {
			LOG(L_ERR,"ERROR:AAASendMessage: I wasn't able to send request\n");
			session_state_machine( ses, AAA_SEND_FAILED, msg);
			goto error;
		}
	}


	/* free the buffer */
	shm_free(msg->buf.s);
	msg->buf.s = 0;
	msg->buf.len = 0;
	return AAA_ERR_SUCCESS;
error:
	if (tr)
		destroy_transaction( tr );
	if (msg->buf.s) {
		shm_free(msg->buf.s);
		msg->buf.s = 0;
		msg->buf.len = 0;
	}
	return AAA_ERR_FAILURE;
}

