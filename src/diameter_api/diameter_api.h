/*
 * $Id: diameter_api.h,v 1.10 2003/04/22 19:58:41 andrei Exp $
 *
 * 2002-10-04 created by illya (komarov@fokus.gmd.de)
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



#ifndef _AAA_DIAMETER_API_H
#define _AAA_DIAMETER_API_H

#include "../diameter_msg/diameter_msg.h"




/****************************** TYPES ***************************************/


/* types of timeout - passed when a Tout handler is called
 */
#define ANSWER_TIMEOUT_EVENT  1
#define SESSION_TIMEOUT_EVENT 2


/*  */
typedef enum {
	_B_FALSE,
	_B_TRUE
}boolean_t;


/* possible identities for AAA parties */
enum AAA_STATUS {
	AAA_UNDEFINED = 0,
	AAA_CLIENT = 1,
	AAA_SERVER = 2,
	AAA_SERVER_STATELESS = 2,
	AAA_SERVER_STATEFULL = 3,
};


/* The following defines the possible security characteristics for a host
 */
typedef enum {
	AAA_SEC_NOT_DEFINED = -2,
	AAA_SEC_NOT_CONNECTED = -1,
	AAA_SEC_NO_SECURITY = 0,
	AAA_SEC_CMS_SECURITY = 1,
	AAA_SEC_CMS_PROXIED = 2
} AAASecurityStatus;


/* The following structure is returned by the dictionary entry lookup
 * functions */
typedef struct dictionaryEntry {
	AAA_AVPCode    avpCode;
	char*          avpName;
	AAA_AVPDataType     avpType;
	AAAVendorId    vendorId;
	AAA_AVPFlag    flags;
} AAADictionaryEntry;



/*********************************** FUNCTIONS *******************************/


#define get_my_appref() \
	((AAAApplicationRef)&exports)


AAAReturnCode AAAOpen();


AAAReturnCode AAAClose();


AAAReturnCode AAAStartSession(
		AAASessionId **sessionId,
		AAAApplicationRef appReference,
		void *context);


AAAReturnCode AAAEndSession(
		AAASessionId *sessionId );


AAAReturnCode AAASessionTimerStart(
		AAASessionId *sessionId ,
		unsigned int timeout);


AAAReturnCode AAASessionTimerStop(
		AAASessionId *sessionId );

/*
AAAReturnCode AAAAbortSession(
		AAASessionId *sessionId);
*/

AAAReturnCode AAADictionaryEntryFromAVPCode(
		AAA_AVPCode avpCode,
		AAAVendorId vendorId,
		AAADictionaryEntry *entry);


AAAValue AAAValueFromName(
		char *avpName,
		char *vendorName,
		char *valueName);


AAAReturnCode AAADictionaryEntryFromName(
		char *avpName,
		AAAVendorId vendorId,
		AAADictionaryEntry *entry);


AAAValue AAAValueFromAVPCode(
		AAA_AVPCode avpCode,
		AAAVendorId vendorId,
		char *valueName);


const char *AAALookupValueNameUsingValue(
		AAA_AVPCode avpCode,
		AAAVendorId vendorId,
		AAAValue value);


boolean_t AAAGetCommandCode(
		char *commandName,
		AAACommandCode *commandCode,
		AAAVendorId *vendorId);


AAAReturnCode AAASendMessage(
		AAAMessage *message);

/*
AAAReturnCode AAASendAcctRequest(
		AAASessionId *aaaSessionId,
		AAAExtensionId extensionId,
		AAA_AVP_LIST *acctAvpList,
		AAAAcctMessageType msgType);
*/

#endif

