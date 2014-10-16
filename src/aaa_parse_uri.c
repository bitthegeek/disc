
#include <string.h>
#include "str.h"
#include "dprint.h"
#include "aaa_parse_uri.h"

#if 0
#include <stdio.h>
#define DBG printf
#define LOG(lev, fmt, args...) printf(fmt, ## args)
#endif



/* returns 0 in success and -1 on error */
/*
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

int aaa_parse_uri(char* buf, int len, struct aaa_uri* uri)
{
	enum states  {	URI_INIT, URI_HOST, URI_PORT,
					URI_PARAM, URI_PARAM_EAT, URI_PARAM_VAL };
	enum states state;
	char* s;
	char* p;
	char* end;
	char* t;
	str* param;
	long l;
	int port_no;


#define _RANSPORT_STR "ransport="
#define _RANSPORT_LEN  (sizeof(_RANSPORT_STR)-1)
#define _ROTOCOL_STR "rotocol="
#define _ROTOCOL_LEN (sizeof(_ROTOCOL_STR)-1)

#define case_no(x) \
	case x: port_no=port_no*10+(x-'0'); break


	end=buf+len;
	p=buf+6; /* skip over "aaa://" */
	state=URI_INIT;
	s=t=0;
	param=0;
	port_no=0;
	memset(uri, 0, sizeof(struct aaa_uri)); /* zero it all, just to be sure*/
	/*look for sip:*/
	if (len<6) goto error_too_short;
	if (! ( ((buf[0]|0x20)=='a')&&((buf[1]|0x20)=='a')&&((buf[2]|0x20)=='a')&&
		     (buf[3]==':') &&(buf[4]=='/')&&(buf[5]=='/') ) )
		goto error_bad_uri;
	
	for(;p<end; p++){
		switch(state){
			case URI_INIT:
				s=p;
				state=URI_HOST;
				break;
			case URI_HOST:
				switch(*p){
					case ':':
						/* found the host */
						uri->host.s=s;
						uri->host.len=p-s;
						state=URI_PORT;
						s=p+1;
						break;
					case ';':
						uri->host.s=s;
						uri->host.len=p-s;
						state=URI_PARAM;
						s=p+1;
						break;
					case '&':
					case '@':
					case '?':
						goto error_bad_char;
				}
				break;
			case URI_PORT:
				switch(*p){
					case ';':
						uri->port.s=s;
						uri->port.len=p-s;
						uri->port_no=(unsigned short)port_no;
						state=URI_PARAM;
						s=p+1;
						break;
					case_no('0');
					case_no('1');
					case_no('2');
					case_no('3');
					case_no('4');
					case_no('5');
					case_no('6');
					case_no('7');
					case_no('8');
					case_no('9');
					case '&':
					case '@':
					case ':':
					case '?':
					default:
						goto error_bad_char;
				}
				break;
			case URI_PARAM:
				/* supported params:
				 * transport, protocol */ 
				switch(*p){
					case ';':/* new parameter */
					case '=':/* value */
						/* just ignore */
						break;
					case 't':
					case 'T':
						l=end-p-1;
						if ((l>=_RANSPORT_LEN) &&
							(strncasecmp(p+1, _RANSPORT_STR,_RANSPORT_LEN)==0)){
							p+=_RANSPORT_LEN+1;
							t=p;
							param=&uri->transport;
							state=URI_PARAM_VAL;
						}else{
							state=URI_PARAM_EAT;
						}
						break;
					case 'p':
					case 'P':
						l=end-p-1;
						if ((l>=_ROTOCOL_LEN) &&
							(strncasecmp(p+1, _ROTOCOL_STR, _ROTOCOL_LEN)==0)){
							p+=_ROTOCOL_LEN+1;
							t=p;
							param=&uri->protocol;
							state=URI_PARAM_VAL;
						}else{
							state=URI_PARAM_EAT;
						}
						break;
					
				};
				break;
			case URI_PARAM_EAT:
				switch(*p){
					case ';':
						state=URI_PARAM;
						break;
				};
				break;
			case URI_PARAM_VAL:
				switch(*p){
					case ';':
						state=URI_PARAM;
						param->s=t;
						param->len=p-t;
					break;
				}
				break;
			default:
				goto error_bug;
		}
	}
	/*end of uri */
	switch (state){
		case URI_HOST:
			uri->host.s=s;
			uri->host.len=p-s;
			break;
		case URI_PORT:
			uri->port.s=s;
			uri->port.len=p-s;
			uri->port_no=(unsigned short)port_no;
			break;
		case URI_PARAM:
		case URI_PARAM_EAT:
			uri->params.s=s;
			uri->params.len=p-s;
			break;
		case URI_PARAM_VAL:
			uri->params.s=s;
			uri->params.len=p-s;
			param->s=t;
			param->len=p-t;
			break;
		case URI_INIT: goto error_too_short;
		default:
			goto error_bug;
	}
	
	/* do stuff */
	DBG("parsed uri:\n host=<%.*s>(%d)\n"
			" port= %d <%.*s>(%d)\n params=<%.*s>(%d)\n"
			" transport= <%.*s>(%d)\n protocol= <%.*s>(%d)\n",
			uri->host.len, uri->host.s, uri->host.len,
			uri->port_no, uri->port.len, uri->port.s, uri->port.len,
			uri->params.len, uri->params.s, uri->params.len,
			uri->transport.len, uri->transport.s, uri->transport.len,
			uri->protocol.len, uri->protocol.s, uri->protocol.len
		);
	return 0;
	
error_too_short:
	LOG(L_ERR, "ERROR: aaa_parse_uri: uri too short: <%.*s> (%d)\n",
			len, buf, len);
	return -1;
error_bad_char:
	LOG(L_ERR, "ERROR: aaa_parse_uri: bad char '%c' in state %d"
			" parsed: <%.*s> (%d) / <%.*s> (%d)\n",
			*p, state, (p-buf), buf, (p-buf), len, buf, len);
	return -1;
error_bad_uri:
	LOG(L_ERR, "ERROR: aaa_parse_uri: bad uri,  state %d"
			" parsed: <%.*s> (%d) / <%.*s> (%d)\n",
			 state, (p-buf), buf, (p-buf), len, buf, len);
	return -1;
error_bug:
	LOG(L_CRIT, "BUG: aaa_parse_uri: bad  state %d"
			" parsed: <%.*s> (%d) / <%.*s> (%d)\n",
			 state, (p-buf), buf, (p-buf), len, buf, len);
	return -1;
}

#if 0

int main (int argc, char** argv)
{

	int r;
	struct aaa_uri uri;

	if (argc<2){
		printf("usage:    %s  uri [, uri...]\n", argv[0]);
		exit(1);
	}
	
	for (r=1; r<argc; r++){
		if (aaa_parse_uri(argv[r], strlen(argv[r]), &uri)<0){
			printf("error: parsing %s\n", argv[r]);
		}
	}
	return 0;
}

#endif
