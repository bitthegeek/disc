/*
 * $Id: print.c,v 1.15 2003/04/22 19:58:41 andrei Exp $
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
 * Example aaa module (it does not do anything useful)
 *
 * History:
 * --------
 *  2003-03-28  created by andrei
 *  2003-04-14  added module params (andrei)
 */

#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

#include "../../../dprint.h"
#include "../../../aaa_module.h"
#include "../../../diameter_api/diameter_api.h"


static int mod_init();
static void mod_destroy();
static int mod_msg(AAAMessage *msg, void *context);
static int mod_tout(int event, AAASessionId* sId, void *context);

static int foo=0;
static char* bar="default";

static struct module_param params[]={
	{"foo",  INT_TYPE, &foo},
	{"bar",  STR_TYPE, &bar},
	{0,0,0} /* null terminated array */
};


struct module_exports exports = {
	"print",
	AAA_CLIENT,
	4,
	DOES_AUTH|DOES_ACCT,
	params,
	
	mod_init,
	mod_destroy,
	mod_msg,
	mod_tout
};



pthread_t print_th;




void *print_worker(void *attr)
{
	AAASessionId *sID;
	AAAMessage   *req;
	AAA_AVP      *avp;
	int i;

	sleep(5);

	while (1) {
		for (i=0;i<100;i++) {
			sID = 0;
			req = 0;
			avp = 0;

			if (AAAStartSession( &sID, get_my_appref(), 0)!=AAA_ERR_SUCCESS)
				goto error;

			if ( (req=AAANewMessage( 456, 4, sID, 0))==0)
				goto error;

			if ( (avp=AAACreateAVP( AVP_Destination_Realm, 0, 0,
			"gmd.de", 6, AVP_DONT_FREE_DATA))==0)
				goto error;

			AAAAddAVPToMessage( req, avp, req->orig_realm);

			if (AAASendMessage( req )!=AAA_ERR_SUCCESS)
				AAAEndSession( sID );

			AAAFreeMessage( &req );

			sleep(50);
			continue;
error:
			if (sID)
				AAAEndSession( sID );
			if (req)
				AAAFreeMessage( &req );
		}
		//sleep(60);
	}
	return 0;
}


int mod_init()
{
	DBG("print module : initializing...\n");
	DBG("print module : foo=%d, bar=%s\n", foo, bar);
	if (pthread_create( &print_th, /*&attr*/ 0, &print_worker, 0)!=0){
		LOG(L_ERR,"ERROR:mod_init: cannot create worker thread\n");
		return -1;
	}

	return 0;
}


void mod_destroy()
{
	DBG("\n print module: selfdestruct triggered\n");
	pthread_cancel( print_th );
}


int mod_msg(AAAMessage *msg, void *context)
{
	DBG("\n print module: mod_msg() called\n");
	if (msg->commandCode==456) {
		DBG(" print module: got answer on my request\n");
		AAASessionTimerStart( msg->sId, 5);
	}
	return 1;
}


int mod_tout(int event, AAASessionId* sId, void *context)
{
	DBG("\n print module: mod_tout() called\n");
	if (event==SESSION_TIMEOUT_EVENT) {
		DBG(" print module: session %p expired -> destroy it\n",sId);
		AAAEndSession( sId );
	} else {
		DBG(" print module: no response to request :-( -> destroy sesion\n");
		AAAEndSession( sId );
	}

	return 1;
}


