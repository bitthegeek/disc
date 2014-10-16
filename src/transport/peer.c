/*
 * $Id: peer.c,v 1.41 2003/08/25 14:52:02 bogdan Exp $
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
 *
 * History:
 *
 * 2003-02-18  created by bogdan
 * 2003-03-12  converted to shm_malloc/shm_free (andrei)
 * 2003-03-13  converted to locking.h/gen_lock_t (andrei)
 * 2003-08-25  connection_id changes now when connection close (bogdan)
 */




#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include "../mem/shm_mem.h"
#include "../dprint.h"
#include "../str.h"
#include "../utils.h"
#include "../locking.h"
#include "../globals.h"
#include "../timer.h"
#include "../msg_queue.h"
#include "../aaa_module.h"
#include "../diameter_api/diameter_api.h"
#include "ip_addr.h"
#include "resolve.h"
#include "tcp_shell.h"
#include "peer.h"


#define PEER_TIMER_STEP  1
#define RECONN_TIMEOUT   60*2
#define WAIT_CER_TIMEOUT 10
#define SEND_DWR_TIMEOUT 35

#define MAX_APPID_PER_PEER 64


#define list_add_tail_safe( _lh_ , _list_ ) \
	do{ \
		if ( !(_lh_)->next ) { \
			lock_get( (_list_)->mutex );\
			list_add_tail( (_lh_), &((_list_)->lh) );\
			lock_release( (_list_)->mutex );\
		} \
	}while(0);

#define list_del_safe( _lh_ , _list_ ) \
	do{ \
		if ( (_lh_)->next ) {\
			lock_get( (_list_)->mutex );\
			list_del_zero( (_lh_) );\
			(_lh_)->next = (_lh_)->prev = 0;\
			lock_release( (_list_)->mutex );\
		}\
	}while(0);




void destroy_peer( struct peer *p);
void peer_timer_handler(unsigned int ticks, void* param);
static int build_msg_buffers(struct p_table *table);

struct p_table      *peer_table = 0;
static struct timer *reconn_timer = 0;
static struct timer *wait_cer_timer = 0;
static struct safe_list_head activ_peers;






struct p_table *init_peer_manager( unsigned int trans_hash_size )
{
	struct p_table *peer_table;

	/* allocate the peer vector */
	peer_table = (struct p_table*)shm_malloc( sizeof(struct p_table) );
	if (!peer_table) {
		LOG(L_ERR,"ERROR:init_peer_manager: no more free memory!\n");
		goto error;
	}
	memset( peer_table, 0, sizeof(struct p_table) );

	/* size of the transaction hash table */
	peer_table->trans_hash_size = trans_hash_size;

	/* init the peer list */
	INIT_LIST_HEAD( &(peer_table->peers) );

	/* build and set the mutex */
	peer_table->mutex = create_locks(1);
	if (!peer_table->mutex) {
		LOG(L_ERR,"ERROR:init_peer_manager: cannot create lock!!\n");
		goto error;
	}

	/* creates the msg buffers */
	if (build_msg_buffers(peer_table)==-1) {
		LOG(L_ERR,"ERROR:init_peer_manager: cannot build msg buffers!!\n");
		goto error;
	}

	/* create a reconnect timer list */
	reconn_timer = new_timer_list();
	if (!reconn_timer) {
		LOG(L_ERR,"ERROR:init_peer_manager: cannot create reconnect "
			"timer list!!\n");
		goto error;
	}

	/* create a wait CER timer list */
	wait_cer_timer = new_timer_list();
	if (!wait_cer_timer) {
		LOG(L_ERR,"ERROR:init_peer_manager: cannot create wait CER "
			"timer list!!\n");
		goto error;
	}

	/* create activ peers list */
	INIT_LIST_HEAD( &(activ_peers.lh) );
	activ_peers.mutex = create_locks( 1 );
	if ( !activ_peers.mutex ) {
		LOG(L_ERR,"ERROR:init_peer_manager: cannot create lock for activ"
			" peers list!!\n");
		goto error;
	}

	/* set a delete timer function */
	if ( register_timer( peer_timer_handler, 0, PEER_TIMER_STEP)==-1 ) {
		LOG(L_ERR,"ERROR:init_peer_manager: cannot register delete timer!!\n");
		goto error;
	}

	LOG(L_INFO,"INFO:init_peer_manager: peer manager started\n");
	return peer_table;
error:
	LOG(L_INFO,"INFO:init_peer_manager: FAILED to start peer manager\n");
	return 0;
}




void destroy_peer_manager(struct p_table *peer_table)
{
	struct list_head  *lh, *foo;
	struct peer       *p;

	if (peer_table) {
		/* destroy all the peers */
		list_for_each_safe( lh, foo, &(peer_table->peers)) {
			/* free the peer */
			list_del_zero( lh );
			p = list_entry( lh, struct peer, all_peer_lh);
			destroy_peer( p );
		}
		/* destroy the mutex */
		if (peer_table->mutex)
			destroy_locks( peer_table->mutex, 1);
		/* free the buffers */
		if (peer_table->std_req.s)
			shm_free(peer_table->std_req.s);
		if (peer_table->std_ans.s)
			shm_free(peer_table->std_ans.s);
		if (peer_table->ce_avp_ipv4.s)
			shm_free(peer_table->ce_avp_ipv4.s);
		if (peer_table->ce_avp_ipv6.s)
			shm_free(peer_table->ce_avp_ipv6.s);
		if (peer_table->dpr_avp.s)
			shm_free(peer_table->dpr_avp.s);
		/* destroy the table */
		shm_free( peer_table );
	}

	if (wait_cer_timer)
		destroy_timer_list( wait_cer_timer );

	if (reconn_timer)
		destroy_timer_list( reconn_timer );

	destroy_locks( activ_peers.mutex, 1);

	LOG(L_INFO,"INFO:destroy_peer_manager: peer manager stoped\n");
}




static int build_msg_buffers(struct p_table *table)
{
	struct aaa_module *mod;
	int  nr_auth_app;
	int  nr_acct_app;
	char *ptr;

	/* standard request */
	table->std_req.len = AAA_MSG_HDR_SIZE +            /* header */
		AVP_HDR_SIZE(0) + to_32x_len(aaa_identity.len) +  /* origin-host  */
		AVP_HDR_SIZE(0) + to_32x_len(aaa_realm.len);      /* origin-realm */
	table->std_req.s = shm_malloc( table->std_req.len );
	if (!table->std_req.s)
		goto error;
	ptr = table->std_req.s;
	memset( ptr, 0, table->std_req.len);
	/* diameter header */
	ptr[0] = 0x01;
	ptr[4] = 0x80;
	ptr += AAA_MSG_HDR_SIZE;
	/* origin host AVP */
	((unsigned int*)ptr)[0] = htonl(264);
	((unsigned int*)ptr)[1] = htonl( AVP_HDR_SIZE(0) + aaa_identity.len );
	ptr[4] = 1<<6;
	ptr += AVP_HDR_SIZE(0);
	memcpy( ptr, aaa_identity.s, aaa_identity.len);
	ptr += to_32x_len(aaa_identity.len);
	/* origin realm AVP */
	((unsigned int*)ptr)[0] = htonl(296);
	((unsigned int*)ptr)[1] = htonl( AVP_HDR_SIZE(0) + aaa_realm.len );
	ptr[4] = 1<<6;
	ptr += AVP_HDR_SIZE(0);
	memcpy( ptr, aaa_realm.s, aaa_realm.len);
	ptr += to_32x_len(aaa_realm.len);


	/* standard answer */
	table->std_ans.len = AAA_MSG_HDR_SIZE +            /* header */
		AVP_HDR_SIZE(0) + 4 +                             /* result-code  */
		AVP_HDR_SIZE(0) + to_32x_len(aaa_identity.len) +  /* origin-host  */
		AVP_HDR_SIZE(0) + to_32x_len(aaa_realm.len);      /* origin-realm */
	table->std_ans.s = shm_malloc( table->std_ans.len );
	if (!table->std_ans.s)
		goto error;
	ptr = table->std_ans.s;
	memset( ptr, 0, table->std_ans.len);
	/* diameter header */
	ptr[0] = 0x01;
	ptr[4] = 0x00;
	ptr += AAA_MSG_HDR_SIZE;
	/* result code AVP */
	((unsigned int*)ptr)[0] = htonl(268);
	((unsigned int*)ptr)[1] = htonl( AVP_HDR_SIZE(0) + 4 );
	ptr[4] = 1<<6;
	ptr += AVP_HDR_SIZE(0) + 4;
	/* origin host AVP */
	((unsigned int*)ptr)[0] = htonl(264);
	((unsigned int*)ptr)[1] = htonl( AVP_HDR_SIZE(0) + aaa_identity.len );
	ptr[4] = 1<<6;
	ptr += AVP_HDR_SIZE(0);
	memcpy( ptr, aaa_identity.s, aaa_identity.len);
	ptr += to_32x_len(aaa_identity.len);
	/* origin realm AVP */
	((unsigned int*)ptr)[0] = htonl(296);
	((unsigned int*)ptr)[1] = htonl( AVP_HDR_SIZE(0) + aaa_realm.len );
	ptr[4] = 1<<6;
	ptr += AVP_HDR_SIZE(0);
	memcpy( ptr, aaa_realm.s, aaa_realm.len);
	ptr += to_32x_len(aaa_realm.len);


	/* CE avps IPv4 */
	/* coutn the auth_app_ids and acct_app_ids */
	nr_auth_app = nr_acct_app = 0;
	for(mod=modules;mod;mod=mod->next) {
		if ( mod->exports->flags&DOES_AUTH )
			nr_auth_app++;
		if ( mod->exports->flags&DOES_ACCT )
			nr_acct_app++;
	}
	if (do_relay) {
		nr_auth_app++;
		nr_acct_app++;
	}
	/* build the avps */
	table->ce_avp_ipv4.len =
		AVP_HDR_SIZE(0) + 4 +                            /* host-ip-address */
		AVP_HDR_SIZE(0) + 4 +                            /* vendor-id  */
		AVP_HDR_SIZE(0) + to_32x_len(product_name.len) + /* product-name */
		nr_auth_app*(AVP_HDR_SIZE(0) + 4) +              /* auth-app-id */
		nr_acct_app*(AVP_HDR_SIZE(0) + 4);               /* acc-app-id */
	table->ce_avp_ipv4.s = shm_malloc( table->ce_avp_ipv4.len );
	if (!table->ce_avp_ipv4.s)
		goto error;
	ptr = table->ce_avp_ipv4.s;
	memset( ptr, 0, table->ce_avp_ipv4.len);
	/* host-ip-address AVP */
	((unsigned int*)ptr)[0] = htonl(257);
	((unsigned int*)ptr)[1] = htonl( AVP_HDR_SIZE(0) + 4 );
	ptr[4] = 1<<6;
	ptr += AVP_HDR_SIZE(0) + 4;
	/* vendor-id AVP */
	((unsigned int*)ptr)[0] = htonl(266);
	((unsigned int*)ptr)[1] = htonl( AVP_HDR_SIZE(0) + 4 );
	ptr[4] = 1<<6;
	ptr += AVP_HDR_SIZE(0);
	((unsigned int*)ptr)[0] = htonl( vendor_id );
	ptr += 4;
	/* product-name AVP */
	((unsigned int*)ptr)[0] = htonl(269);
	((unsigned int*)ptr)[1] = htonl( AVP_HDR_SIZE(0) + product_name.len );
	ptr += AVP_HDR_SIZE(0);
	memcpy( ptr, product_name.s, product_name.len);
	ptr += to_32x_len(product_name.len);
	/* auth-app-id AVP */
	for(mod=modules;mod;mod=mod->next)
		if ( mod->exports->flags&DOES_AUTH ) {
			((unsigned int*)ptr)[0] = htonl(258);
			((unsigned int*)ptr)[1] = htonl( AVP_HDR_SIZE(0) + 4 );
			ptr[4] = 1<<6;
			ptr += AVP_HDR_SIZE(0);
			((unsigned int*)ptr)[0] = htonl( mod->exports->app_id );
			ptr += 4;
		}
	if (do_relay) {
		((unsigned int*)ptr)[0] = htonl(258);
		((unsigned int*)ptr)[1] = htonl( AVP_HDR_SIZE(0) + 4 );
		ptr[4] = 1<<6;
		ptr += AVP_HDR_SIZE(0);
		((unsigned int*)ptr)[0] = htonl( AAA_APP_RELAY );
		ptr += 4;
	}
	/* acc-app-id AVP */
	for(mod=modules;mod;mod=mod->next)
		if ( mod->exports->flags&DOES_ACCT ) {
			((unsigned int*)ptr)[0] = htonl(259);
			((unsigned int*)ptr)[1] = htonl( AVP_HDR_SIZE(0) + 4 );
			ptr[4] = 1<<6;
			ptr += AVP_HDR_SIZE(0);
			((unsigned int*)ptr)[0] = htonl( mod->exports->app_id );
			ptr += 4;
		}
	if (do_relay) {
		((unsigned int*)ptr)[0] = htonl(259);
		((unsigned int*)ptr)[1] = htonl( AVP_HDR_SIZE(0) + 4 );
		ptr[4] = 1<<6;
		ptr += AVP_HDR_SIZE(0);
		((unsigned int*)ptr)[0] = htonl( AAA_APP_RELAY );
		ptr += 4;
	}


	/* CE avps IPv6 */
	table->ce_avp_ipv6.len = table->ce_avp_ipv4.len + 12;
	table->ce_avp_ipv6.s = shm_malloc( table->ce_avp_ipv6.len );
	if (!table->ce_avp_ipv6.s)
		goto error;
	ptr = table->ce_avp_ipv6.s;
	memset( ptr, 0, table->ce_avp_ipv6.len);
	/* copy the host-ip avp */
	memcpy( ptr, table->ce_avp_ipv4.s, AVP_HDR_SIZE(0) );
	((unsigned int*)ptr)[1] = htonl( AVP_HDR_SIZE(0) + 16 ); /* update len */
	ptr += AVP_HDR_SIZE(0) + 16;
	/* copy the rest of the avps */
	memcpy( ptr, table->ce_avp_ipv4.s+AVP_HDR_SIZE(0)+4,
		table->ce_avp_ipv4.len-AVP_HDR_SIZE(0)-4);


	/* DPR avp */
	table->dpr_avp.len = AVP_HDR_SIZE(0) + 4; /* disconnect cause  */
	table->dpr_avp.s = shm_malloc( table->dpr_avp.len );
	if (!table->dpr_avp.s)
		goto error;
	ptr = table->dpr_avp.s;
	memset( ptr, 0, table->dpr_avp.len);
	/* disconnect cause AVP */
	((unsigned int*)ptr)[0] = htonl(273);
	((unsigned int*)ptr)[1] = htonl( AVP_HDR_SIZE(0) + 4 );
	ptr[4] = 1<<6;
	ptr += AVP_HDR_SIZE(0) + 4;

	return 1;
error:
	LOG(L_ERR,"ERROR:build_msg_buffers: no more free memory\n");
	return -1;
}




/* a new peer is created and added. The name of the peer host is given and the 
 * offset of the realm inside the host name. The host name and realm are copied
 * locally */
struct peer* add_peer( str *aaa_identity, str *host, unsigned int port )
{
	static struct hostent* ht;
	struct peer *p;
	int i;

	p = 0;

	if (!peer_table || !host->s || !host->len || !aaa_identity->s ||
	!aaa_identity->len ) {
		LOG(L_ERR,"ERROR:add_peer: one of the param is null!!!!\n");
		goto error;
	}

	p = (struct peer*)shm_malloc( sizeof(struct peer) + aaa_identity->len +
		host->len + 1 );
	if(!p) {
		LOG(L_ERR,"ERROR:add_peer: no more free memory!\n");
		goto error;
	}
	memset(p,0,sizeof(struct peer) + aaa_identity->len + host->len + 1 );

	/* create a new mutex for the peer */
	p->mutex = create_locks( 1 );
	if (!p->mutex) {
		LOG(L_ERR,"ERROR:add_peer: cannot build lock!\n");
		goto error;
	}

	/* fill the peer structure */
	p->tl.payload = p;
	p->aaa_identity.s = (char*)p + sizeof(struct peer);
	p->aaa_identity.len = aaa_identity->len;
	p->aaa_host.s = p->aaa_identity.s + aaa_identity->len;
	p->aaa_host.len = host->len;

	/* copy the aaa_identity converted to lower case */
	for(i=0;i<aaa_identity->len;i++)
		p->aaa_identity.s[i] = tolower( aaa_identity->s[i] );
	/* copy the host name converted to lower case */
	for(i=0;i<host->len;i++)
		p->aaa_host.s[i] = tolower( host->s[i] );

	/* resolve the host name */
	ht = resolvehost( p->aaa_host.s );
	if (!ht) {
		LOG(L_ERR,"ERROR:add_peer: cannot resolve host \"%s\"!\n",
			p->aaa_host.s);
		goto error;
	}
	/* fill the IP_ADDR structure */
	hostent2ip_addr( &p->ip, ht, 0);
	/* set the port */
	p->port = htons(port);

	/* get a thread to listen for this peer */
	p->fd = get_new_receive_thread();

	/* build the hash tbale for the transactions */
	p->trans_table = build_htable( peer_table->trans_hash_size );

	/* init the end-to-end-ID counter */
	p->endtoendID  = ((unsigned int)time(0))<<20;
	p->endtoendID |= ((unsigned int)rand( ))>>12;


	/* insert the peer into the list */
	lock_get( peer_table->mutex );
	list_add_tail( &(p->all_peer_lh), &(peer_table->peers) );
	lock_release( peer_table->mutex );

	/* give the peer to the designated thread */
	write_command( p->fd, ADD_PEER_CMD, 0, p, 0);

	return p;
error:
	if (p) shm_free(p);
	return 0;
}




void destroy_peer( struct peer *p)
{
	if (p) {
		/* destroy transaction hash table */
		if (p->trans_table)
			destroy_htable( p->trans_table );
		/* free the supported application ids */
		if (p->supp_acct_app_ids)
			shm_free( p->supp_acct_app_ids );
		if (p->supp_auth_app_ids)
			shm_free( p->supp_auth_app_ids );
		/* free the origin realm */
		if (p->aaa_realm.s)
			shm_free( p->aaa_realm.s );
		/* free the lock */
		if (p->mutex)
			destroy_locks( p->mutex, 1 );
		shm_free( p );
	}
}



void static peer_trans_timeout_f( struct trans *tr )
{
	write_command( tr->out_peer->fd, TIMEOUT_PEER_CMD,
		PEER_TR_TIMEOUT, tr->out_peer, (void*)tr->info);
}



static inline int safe_write(struct peer *p, char *buf, unsigned int len)
{
	int n;

	/*
	for(n=0;n<len;n++) {
		DBG(" %02x",(unsigned char)buf[n]);
		if ((n&15)==0)
			DBG("\n");
	}*/

	while( (n=write(p->sock,buf,len))==-1 ) {
		if (errno==EINTR)
			continue;
		LOG(L_ERR,"ERROR:safe_write: write returned error: %s\n",
			strerror(errno));
		p->state = PEER_ERROR;
		close_peer( p );
		return -1;
	}

	if (n!=len) {
		LOG(L_CRIT,"ERROR:safe_write: BUG!!! write gave no error but wrote"
			" less than asked\n");
		p->state = PEER_ERROR;
		close_peer( p );
	}
	return 1;
}




int send_req_to_peer(struct trans *tr , struct peer *p)
{
	unsigned int ete;
	str *foo;
	str s;

	lock_get( p->mutex );
	if ( p->state!=PEER_CONN && p->state!=PEER_WAIT_DWA) {
		LOG(L_INFO,"ERROR:send_req_to_peer: peer is not connected\n");
		lock_release( p->mutex);
		return -1;
	}
	/* peer available for sending */
	DBG("peer \"%.*s\" available for sending\n",p->aaa_identity.len,
			p->aaa_identity.s);
	/* get a new end-to-end ID for computing the hash code */
	if (tr->in_peer) {
		/* this is forwarding*/
		ete = ((unsigned int*)tr->req->s)[4];
	}else {
		/* sending local req */
		ete = p->endtoendID++;
		((unsigned int*)tr->req->s)[4] = ete;
	}
	s.s = (char*)&ete;
	s.len = END_TO_END_IDENTIFIER_SIZE;
	tr->linker.hash_code = hash( &s , p->trans_table->hash_size );
	/* insert into trans hash table */
	add_cell_to_htable( p->trans_table, &(tr->linker) );
	/* the hash label is used as hop-by-hop ID */
	((unsigned int*)tr->req->s)[3] = tr->linker.label;
	/* things that I want to remember or not */
	foo = tr->req;
	tr->out_peer = p;
	tr->req = 0;
	/* transaction will be on more ref from timer list; the ref from this
	 * thread will be inherided by hash tabel */
	atomic_inc( &tr->ref_cnt );
	/* start the timeout timer */
	add_to_timer_list( &(tr->timeout) , tr_timeout_timer ,
		get_ticks()+TR_TIMEOUT_TIMEOUT );
	/* send it- I'm not intereseted in the return code -if it's error, the
	 * peer will be automaticly closed by safe_write and this transaction will
	 * give timeout */
	safe_write( p, foo->s, foo->len);
	lock_release( p->mutex);
	return 1;
}





int send_res_to_peer( str *buf, struct peer *p)
{
	lock_get( p->mutex );
	if ( p->state==PEER_CONN || p->state==PEER_WAIT_DWA) {
		if (safe_write( p, buf->s, buf->len)!=-1) {
			lock_release( p->mutex);
			return 1;
		}
	} else {
		LOG(L_INFO,"ERROR:send_res_to_peer: peer is not connected\n");
	}

	lock_release( p->mutex);
	return -1;
}




/*  sends out a buffer*/
inline int internal_send_request( str *buf, struct peer *p)
{
	struct trans *tr;
	str s;

	/* build a new outgoing transaction for this request */
	if ((tr=create_transaction( 0, 0, peer_trans_timeout_f))==0) {
		LOG(L_ERR,"ERROR:internal_send_request: cannot create a new"
			" transaction!\n");
		return -1;
	}
	tr->info = p->conn_cnt;
	tr->out_peer = p;

	/* generate a new end-to-end id */
	((unsigned int *)buf->s)[4] = p->endtoendID++;

	/* link the transaction into the hash table ; use end-to-end-ID for
	 * calculating the hash_code */
	s.s = buf->s+(4*4);
	s.len = END_TO_END_IDENTIFIER_SIZE;
	tr->linker.hash_code = hash( &s, p->trans_table->hash_size );
	add_cell_to_htable( p->trans_table, (struct h_link*)tr );
	/* the hash label is used as hop-by-hop ID */
	((unsigned int*)buf->s)[3] = tr->linker.label;

	/* transaction will be on more ref from timer list; the ref from this
	 * thread will be inherided by hash tabel */
	atomic_inc( &tr->ref_cnt );
	/* start the timeout timer */
	add_to_timer_list( &(tr->timeout) , tr_timeout_timer ,
		get_ticks()+TR_TIMEOUT_TIMEOUT );

	/* send the request */
	safe_write( p, buf->s, buf->len);

	return 1;
}



inline int internal_send_response( str *res, str *req, unsigned int res_code,
																struct peer *p)
{
	/* set the result code  AVP value */
	((unsigned int*)res->s)[(AAA_MSG_HDR_SIZE+AVP_HDR_SIZE(0))>>2] =
		htonl( res_code );

	/* set the length and the command code */
	((unsigned int*)res->s)[0] |= MASK_MSG_CODE&htonl( res->len );
	((unsigned int*)res->s)[1] |= MASK_MSG_CODE&((unsigned int*)req->s)[1];

	/* set the same application, hop-by-hop and end-to-end Id as in request */
	((unsigned int*)res->s)[2] = ((unsigned int*)req->s)[2];
	((unsigned int*)res->s)[3] = ((unsigned int*)req->s)[3];
	((unsigned int*)res->s)[4] = ((unsigned int*)req->s)[4];

	/* send the message */
	if ( safe_write( p, res->s, res->len)==-1 ) {
		LOG(L_ERR,"ERROR:internal_send_response: tcp_send_unsafe "
			"returned error!\n");
		return -1;
	}

	return 1;
}



int send_cer( struct peer *dst_peer)
{
	char *ptr;
	str *ce_avp;
	str cer;
	int ret;

#ifdef USE_IPV6
	if (dst_peer->local_ip.af==AF_INET6)
		ce_avp = &(peer_table->ce_avp_ipv6);
	else
#endif
		ce_avp = &(peer_table->ce_avp_ipv4);
	cer.len = peer_table->std_req.len + ce_avp->len;
	cer.s = shm_malloc( cer.len );
	if (!cer.s) {
		LOG(L_ERR,"ERROR:send_cer: no more free memory\n");
		goto error;
	}
	ptr = cer.s;
	/* copy the ce specific AVPs */
	memcpy( ptr, peer_table->std_req.s, peer_table->std_req.len );
	((unsigned int*)ptr)[0] |= htonl( cer.len );
	((unsigned int*)ptr)[1] |= CE_MSG_CODE;
	ptr += peer_table->std_req.len;
	/* set the correct address */
	memcpy( ptr, ce_avp->s, ce_avp->len );
	memcpy( ptr+AVP_HDR_SIZE(0), dst_peer->local_ip.u.addr,
		dst_peer->local_ip.len);

	/* send the buffer */
	ret = internal_send_request( &cer, dst_peer);

	shm_free( cer.s );
	return ret;
error:
	return -1;
}




int send_cea( str *cer, unsigned int result_code, struct peer *p)
{
	char *ptr;
	str *ce_avp;
	str cea;
	int ret;

#ifdef USE_IPV6
	if (p->local_ip.af==AF_INET6)
		ce_avp = &(peer_table->ce_avp_ipv6);
	else
#endif
		ce_avp = &(peer_table->ce_avp_ipv4);
	cea.len = peer_table->std_ans.len + ce_avp->len;
	cea.s = shm_malloc( cea.len );
	if (!cea.s) {
		LOG(L_ERR,"ERROR:send_cer: no more free memory\n");
		goto error;
	}
	ptr = cea.s;
	/* copy the standart answer part */
	memcpy( ptr, peer_table->std_ans.s, peer_table->std_ans.len );
	ptr += peer_table->std_ans.len;
	/* set the correct address into host-ip-address AVP */
	memcpy( ptr, ce_avp->s, ce_avp->len );
	memcpy( ptr+AVP_HDR_SIZE(0), p->local_ip.u.addr, p->local_ip.len);

	/* send the buffer */
	ret = internal_send_response( &cea, cer, result_code, p);

	shm_free( cea.s );
	return ret;
error:
	return -1;
}



/* if buf containes a request, the response code that has to be sent back as
 * answer will be returned;
 * if it's an answer, the contained respponse code will be returned.
 */
unsigned int process_ce( struct peer *p, str *buf , int is_req)
{
	static unsigned int rpl_pattern = 0x0000003f;
	static unsigned int req_pattern = 0x0000003e;
	unsigned int auth_app_ids[ MAX_APPID_PER_PEER ];
	unsigned int acct_app_ids[ MAX_APPID_PER_PEER ];
	unsigned int nr_auth_app_ids;
	unsigned int nr_acct_app_ids;
	struct aaa_module *mod;
	unsigned int mask;
	unsigned int code;
	char *ptr;
	char *foo;
	char *realm;

	mask = 0;
	realm = 0;
	code = AAA_NO_COMMON_APPLICATION;
	nr_auth_app_ids = 0;
	nr_acct_app_ids = 0;

	for_all_AVPS_do_switch( buf , foo , ptr ) {
		case 268: /* result_code */
			set_bit_in_mask( mask, 0);
			code = ntohl( ((unsigned int *)(ptr+AVP_HDR_SIZE(ptr[4])))[0] );
			DBG("DEBUG:process_ce: CEA has code %d\n", code);
			if (code!=AAA_SUCCESS)
				return code;
			break;
		case 264: /* orig host */
			set_bit_in_mask( mask, 1);
			break;
		case 296: /* orig realm */
			set_bit_in_mask( mask, 2);
			realm = ptr;
			break;
		case 257: /* host ip address */
			set_bit_in_mask( mask, 3);
			break;
		case 266: /* vendor ID */
			set_bit_in_mask( mask, 4);
			break;
		case 269: /*product name */
			set_bit_in_mask( mask, 5);
			break;
		case 259: /*acc app id*/
			if (nr_acct_app_ids==MAX_APPID_PER_PEER) {
				LOG(L_ERR,"ERROR:process_ce: remote peer advertise more "
					"than %d acct app ids! IGNORING\n",MAX_APPID_PER_PEER);
				break;
			}
			acct_app_ids[nr_acct_app_ids++] = ntohl(((unsigned int*)ptr)[2]);
			if ( do_relay || acct_app_ids[nr_acct_app_ids-1]==AAA_APP_RELAY ||
			((mod=find_module( acct_app_ids[nr_acct_app_ids-1]))!=0
			&& mod->exports->flags&DOES_ACCT) )
				code = AAA_SUCCESS;
			break;
		case 258: /*auth app id*/
			if (nr_auth_app_ids==MAX_APPID_PER_PEER) {
				LOG(L_ERR,"ERROR:process_ce: remote peer advertise more "
					"than %d auth app ids! IGNORING\n",MAX_APPID_PER_PEER);
				break;
			}
			auth_app_ids[nr_auth_app_ids++] = ntohl(((unsigned int*)ptr)[2]);
			if ( do_relay || auth_app_ids[nr_auth_app_ids-1]==AAA_APP_RELAY ||
			((mod=find_module( auth_app_ids[nr_auth_app_ids-1]))!=0
			&& mod->exports->flags&DOES_AUTH) )
				code = AAA_SUCCESS;
			break;
	}

	if ( mask!=(is_req?req_pattern:rpl_pattern) ) {
		LOG(L_ERR,"ERROR:process_ce: ce(a|r) has missing avps(%x<>%x)!!\n",
			(is_req?req_pattern:rpl_pattern),mask);
		code = AAA_MISSING_AVP;
	}

	if (code==AAA_SUCCESS) {
		/* copy the auth_app_ids into peer structure */
		if (p->supp_auth_app_ids)
			shm_free( p->supp_auth_app_ids );
		p->supp_auth_app_ids = 0;
		if (nr_auth_app_ids) {
			p->supp_auth_app_ids = (unsigned int*)shm_malloc
				((nr_auth_app_ids+1)*sizeof(unsigned int));
			if (!p->supp_auth_app_ids) {
				LOG(L_ERR,"ERROR:process_ce: cannot allocate memory -> unable "
					"to save auth_app_ids\n");
			} else {
				memcpy( p->supp_auth_app_ids, auth_app_ids,
					nr_auth_app_ids*sizeof(unsigned int));
				p->supp_auth_app_ids[nr_auth_app_ids] = 0;
			}
		}
		/* copy the acct_app_ids into peer structure */
		if (p->supp_acct_app_ids)
			shm_free( p->supp_acct_app_ids );
		p->supp_acct_app_ids = 0;
		if (nr_acct_app_ids) {
			p->supp_acct_app_ids = (unsigned int*)shm_malloc
				((nr_acct_app_ids+1)*sizeof(unsigned int));
			if (!p->supp_acct_app_ids) {
				LOG(L_ERR,"ERROR:process_ce: cannot allocate memory -> unable "
					"to save acct_app_ids\n");
			} else {
				memcpy( p->supp_acct_app_ids, acct_app_ids,
					nr_acct_app_ids*sizeof(unsigned int));
				p->supp_acct_app_ids[nr_acct_app_ids] = 0;
			}
		}
		/* copy the realm */
		p->aaa_realm.len = ntohl(((unsigned int*)(realm))[1]&MASK_MSG_CODE)-
			AVP_HDR_SIZE(realm[4]);
		p->aaa_realm.s = (char*)shm_malloc( p->aaa_realm.len );
		if (!p->aaa_realm.s) {
			LOG(L_ERR,"ERROR:process_ce: cannot allocate memory -> unable "
					"to save origin realm\n");
			p->aaa_realm.len = 0;
		} else {
			memcpy( p->aaa_realm.s, realm + AVP_HDR_SIZE(realm[4]),
				p->aaa_realm.len);
		}
		DBG("DEBUG:process_ce: origin realm = [%.*s](%d)\n",
			p->aaa_realm.len,p->aaa_realm.s,p->aaa_realm.len);
	} else if (code==AAA_NO_COMMON_APPLICATION)
		LOG(L_ERR,"ERROR:process_ce: no commoun applications with peer!\n");

	return code;
}



int send_dwr( struct peer *dst_peer)
{
	char *ptr;
	str dwr;
	int ret;

	dwr.len = peer_table->std_req.len ;
	dwr.s = shm_malloc( dwr.len );
	if (!dwr.s) {
		LOG(L_ERR,"ERROR:send_dwr: no more free memory\n");
		goto error;
	}
	ptr = dwr.s;
	/**/
	memcpy( ptr, peer_table->std_req.s, peer_table->std_req.len );
	((unsigned int*)ptr)[0] |= htonl( dwr.len );
	((unsigned int*)ptr)[1] |= DW_MSG_CODE;

	/* send the buffer */
	ret = internal_send_request( &dwr, dst_peer);

	shm_free( dwr.s );
	return ret;
error:
	return -1;
}



int send_dwa( str *dwr, unsigned int result_code, struct peer *p)
{
	char *ptr;
	str dwa;
	int ret;

	dwa.len = peer_table->std_ans.len;
	dwa.s = shm_malloc( dwa.len );
	if (!dwa.s) {
		LOG(L_ERR,"ERROR:send_dwa: no more free memory\n");
		goto error;
	}
	ptr = dwa.s;

	/* copy the standart part of an answer */
	memcpy( ptr, peer_table->std_ans.s, peer_table->std_ans.len );

	/* send the buffer */
	ret = internal_send_response( &dwa, dwr, result_code, p);

	shm_free( dwa.s );
	return ret;
error:
	return -1;
}



int process_dw( struct peer *p, str *buf , int is_req)
{
	static unsigned int rpl_pattern = 0x00000007;
	static unsigned int req_pattern = 0x00000006;
	unsigned int mask = 0;
	unsigned int n;
	char *ptr;
	char *foo;

	for_all_AVPS_do_switch( buf , foo , ptr ) {
		case 268: /* result_code */
			set_bit_in_mask( mask, 0);
			n = ntohl( ((unsigned int *)ptr)[2] );
			if (n!=AAA_SUCCESS) {
				LOG(L_ERR,"ERROR:process_ce: DWA has a non-success "
					"code : %d\n",n);
				goto error;
			}
			break;
		case 264: /* orig host */
			set_bit_in_mask( mask, 1);
			break;
		case 296: /* orig realm */
			set_bit_in_mask( mask, 2);
			break;
	}

	if ( mask!=(is_req?req_pattern:rpl_pattern) ) {
		LOG(L_ERR,"ERROR:process_dw: dw(a|r) has missing avps(%x<>%x)!!\n",
			(is_req?req_pattern:rpl_pattern),mask);
		goto error;
	}

	return 1;
error:
	return -1;
}



int send_dpr( struct peer *dst_peer, unsigned int disc_cause)
{
	char *ptr;
	str dpr;
	int ret;

	dpr.len = peer_table->std_req.len + peer_table->dpr_avp.len;
	dpr.s = shm_malloc( dpr.len );
	if (!dpr.s) {
		LOG(L_ERR,"ERROR:send_dpr: no more free memory\n");
		goto error;
	}
	ptr = dpr.s;
	/* standar answer part */
	memcpy( ptr, peer_table->std_req.s, peer_table->std_req.len );
	((unsigned int*)ptr)[0] |= htonl( dpr.len );
	((unsigned int*)ptr)[1] |= DP_MSG_CODE;
	ptr += peer_table->std_req.len;
	/* disconnect cause avp */
	memcpy( ptr, peer_table->dpr_avp.s, peer_table->dpr_avp.len );
	((unsigned int*)ptr)[ AVP_HDR_SIZE(0)>>2 ] |= htonl( disc_cause );

	/* send the buffer */
	ret = internal_send_request( &dpr, dst_peer);

	shm_free( dpr.s );
	return ret;
error:
	return -1;
}



int send_dpa( str *dpr, unsigned int result_code, struct peer *p)
{
	char *ptr;
	str dpa;
	int ret;

	dpa.len = peer_table->std_ans.len;
	dpa.s =shm_malloc( dpa.len );
	if (!dpa.s) {
		LOG(L_ERR,"ERROR:send_dwa: no more free memory\n");
		goto error;
	}
	ptr = dpa.s;

	/* copy the standart part of an answer */
	memcpy( ptr, peer_table->std_ans.s, peer_table->std_ans.len );

	/* send the buffer */
	ret = internal_send_response( &dpa, dpr, result_code, p);

	shm_free( dpa.s );
	return ret;
error:
	return -1;
}



int process_dp( struct peer *p, str *buf , int is_req)
{
	static unsigned int rpl_pattern = 0x00000007;
	static unsigned int req_pattern = 0x0000000e;
	unsigned int mask = 0;
	unsigned int n;
	char *ptr;
	char *foo;

	for_all_AVPS_do_switch( buf , foo , ptr ) {
		case 268: /* result_code */
			set_bit_in_mask( mask, 0);
			n = ntohl( ((unsigned int *)ptr)[2] );
			if (n!=AAA_SUCCESS) {
				LOG(L_ERR,"ERROR:process_ce: DPA has a non-success "
					"code : %d\n",n);
				goto error;
			}
			break;
		case 264: /* orig host */
			set_bit_in_mask( mask, 1);
			break;
		case 296: /* orig realm */
			set_bit_in_mask( mask, 2);
			break;
		case 273: /* disconnect cause */
			set_bit_in_mask( mask, 3);
			break;
	}

	if ( mask!=(is_req?req_pattern:rpl_pattern) ) {
		LOG(L_ERR,"ERROR:process_dw: dp(a|r) has missing avps(%x<>%x)!!\n",
			(is_req?req_pattern:rpl_pattern),mask);
		goto error;
	}

	return 1;
error:
	return -1;
}




void dispatch_message( struct peer *p, str *buf)
{
	struct trans *tr;
	unsigned int code;
	int          event;

	/* reset the inactivity time */
	p->last_activ_time = get_ticks();

	/* get message code */
	code = ((unsigned int*)buf->s)[1]&MASK_MSG_CODE;
	/* check the message code */
	switch ( code ) {
		case CE_MSG_CODE:
			event = CER_RECEIVED;
			break;
		case DW_MSG_CODE:
			event = DWR_RECEIVED;
			break;
		case DP_MSG_CODE:
			event = DPR_RECEIVED;
			break;
		default:
			/* it's a session message -> put the message into the queue */
			put_in_queue( buf, p );
			return;
	}

	/* is request or reply? */
	if (buf->s[VER_SIZE+MESSAGE_LENGTH_SIZE]&0x80) {
		/* request */
		peer_state_machine( p, event, buf);
	} else {
		/* response -> find its transaction and remove it from 
		 * hash table (search and remove is an atomic operation) */
		tr = transaction_lookup( p, ((unsigned int*)buf->s)[4],
			((unsigned int*)buf->s)[3]);
		if (!tr) {
			LOG(L_ERR,"ERROR:dispatch_message: respons received, but no"
				" transaction found!\n");
		} else {
			/* destroy the transaction */
			destroy_transaction( tr );
			/* make from a request event a response event */
			event++;
			/* call the peer machine */
			peer_state_machine( p, event, buf );
		}
	}
	/* free the message buffer */
	shm_free( buf->s );
}



inline void reset_peer( struct peer *p)
{
	/* if it's in a timer list -> remove it */
	rmv_from_timer_list( &(p->tl), wait_cer_timer );
	rmv_from_timer_list( &(p->tl), reconn_timer );

	/* reset the socket */
	p->sock = -1;

	/* reset the all flag  except PEER_TO_DESTROY */
	p->flags &= PEER_TO_DESTROY;

	/* put the peer in timer list for reconnection  */
	if (!p->flags&PEER_TO_DESTROY)
		add_to_timer_list( &(p->tl), reconn_timer, get_ticks()+RECONN_TIMEOUT);

	/* remove the peer from activ peer list */
	list_del_safe(  &p->lh , &activ_peers );

	/* reset the buffer for  reading messages */
	p->first_4bytes = 0;
	p->buf_len = 0;
	if (p->buf)
		shm_free(p->buf);
	p->buf = 0;

	/* reset the supported application ids */
	if (p->supp_acct_app_ids)
		shm_free( p->supp_acct_app_ids );
	p->supp_acct_app_ids = 0;
	if (p->supp_auth_app_ids)
		shm_free( p->supp_auth_app_ids );
	p->supp_auth_app_ids = 0;

	/* free the origin realm */
	if (p->aaa_realm.s)
		shm_free( p->aaa_realm.s );
	p->aaa_realm.s = 0;

	/* change the connection id */
	p->conn_cnt++;
}




int peer_state_machine( struct peer *p, enum AAA_PEER_EVENT event, void *ptr)
{
	struct tcp_params *info;
	static char     *err_msg[]= {
		"no error",
		"event - state mismatch",
		"unknown type event",
		"unknown peer state"
	};
	int error_code=0;
	int res;

	if (!p) {
		LOG(L_ERR,"ERROR:peer_state_machine: addressed peer is 0!!\n");
		return -1;
	}

	DBG("DEBUG:peer_state_machine: peer %p, state = %d, event=%d\n",
		p,p->state,event);

	switch (event) {
		case TCP_ACCEPT:
			lock_get( p->mutex );
			switch (p->state) {
				case PEER_UNCONN:
					DBG("DEBUG:peer_state_machine: accepting connection\n");
					/* if peer in reconn timer list-> take it out*/
					rmv_from_timer_list( &(p->tl), reconn_timer );
					if (p->flags&PEER_CONN_IN_PROG) {
						tcp_close( p );
					}
					/* update the peer */
					/* p->conn_cnt++; */ /*in case of undo(bogdan)*/
					info = (struct tcp_params*)ptr;
					p->sock = info->sock;
					memcpy( &p->local_ip, info->local, sizeof(struct ip_addr));
					/* put the peer in wait_cer timer list */
					add_to_timer_list( &(p->tl), wait_cer_timer,
						get_ticks()+WAIT_CER_TIMEOUT);
					/* the new state */
					p->state = PEER_WAIT_CER;
					lock_release( p->mutex );
					break;
				default:
					lock_release( p->mutex );
					error_code = 1;
					goto error;
			}
			break;
		case TCP_CONNECTED:
			lock_get( p->mutex );
			switch (p->state) {
				case PEER_UNCONN:
					DBG("DEBUG:peer_state_machine: connect finished ->"
						" send CER\n");
					/* update the peer */
					/*p->conn_cnt++;*/ /*in case of undo(bogdan)*/
					p->flags &= !PEER_CONN_IN_PROG;
					info = (struct tcp_params*)ptr;
					p->sock = info->sock;
					memcpy( &p->local_ip, info->local, sizeof(struct ip_addr));
					/* send cer */
					if (send_cer( p )!=-1) {
						/* new state */
						p->state = PEER_WAIT_CEA;
					} else {
						tcp_close( p );
						reset_peer( p );
					}
					lock_release( p->mutex );
					break;
				default:
					lock_release( p->mutex );
					error_code = 1;
					goto error;
			}
			break;
		case TCP_CONN_IN_PROG:
			lock_get( p->mutex );
			switch (p->state) {
				case PEER_UNCONN:
					DBG("DEBUG:peer_state_machine: connect in progress\n");
					/* update the peer */
					info = (struct tcp_params*)ptr;
					p->sock = info->sock;
					p->flags |= PEER_CONN_IN_PROG;
					lock_release( p->mutex );
					break;
				default:
					lock_release( p->mutex );
					error_code = 1;
					goto error;
			}
			break;
		case TCP_CONN_FAILED:
			lock_get( p->mutex );
			switch (p->state) {
				case PEER_UNCONN:
					DBG("DEBUG:peer_state_machine: connect failed\n");
					reset_peer( p );
					p->state = PEER_UNCONN;
					lock_release( p->mutex );
					break;
				default:
					lock_release( p->mutex );
					error_code = 1;
					goto error;
			}
			break;
		case TCP_CONN_CLOSE:
			lock_get( p->mutex );
			if (p->state!=PEER_UNCONN || p->flags&PEER_CONN_IN_PROG) {
				DBG("DEBUG:peer_state_machine: closing TCP connection "
					"and reseting peer\n");
				tcp_close( p );
				reset_peer( p );
				/* new state */
				p->state = PEER_UNCONN;
			}
			lock_release( p->mutex );
			break;
		case PEER_CER_TIMEOUT:
			lock_get( p->mutex );
			switch (p->state) {
				case PEER_WAIT_CER:
					DBG("DEBUG:peer_state_machine: CER not received!\n");
					tcp_close( p );
					reset_peer( p );
					p->state = PEER_UNCONN;
					lock_release( p->mutex );
					break;
				default:
					lock_release( p->mutex );
					error_code = 1;
					goto error;
			}
			break;
		case PEER_RECONN_TIMEOUT:
			lock_get( p->mutex );
			switch (p->state) {
				case PEER_UNCONN:
					DBG("DEBUG:peer_state_machine: reconnecting peer\n");
					if (p->flags&PEER_CONN_IN_PROG)
						tcp_close( p );
					write_command( p->fd, CONNECT_CMD, 0, p, 0);
					lock_release( p->mutex );
					break;
				default:
					lock_release( p->mutex );
					DBG("DEBUG:peer_state_machine: no reason to reconnect\n");
			}
			break;
		case PEER_TR_TIMEOUT:
			if ((unsigned int)ptr!=p->conn_cnt) {
				LOG(L_WARN,"WARNING:peer_state_machine: transaction generated"
					" from a prev. connection gave TIMEOUT -> ignoring!\n");
				break;
			}
			lock_get( p->mutex );
			switch (p->state) {
				case PEER_WAIT_DWA:
					list_del_safe( &p->lh , &activ_peers );
				case PEER_WAIT_CEA:
				case PEER_WAIT_DPA:
					DBG("DEBUG:peer_state_machine: transaction timeout\n");
					tcp_close( p );
					reset_peer( p );
					p->state = PEER_UNCONN;
					lock_release( p->mutex );
					break;
				default:
					lock_release( p->mutex );
					error_code = 1;
					goto error;
			}
			break;
		case CEA_RECEIVED:
			lock_get( p->mutex );
			switch (p->state) {
				case PEER_WAIT_CEA:
					DBG("DEBUG:peer_state_machine: CEA received\n");
					if ( process_ce( p, (str*)ptr, 0 )!=AAA_SUCCESS ) {
						tcp_close( p );
						reset_peer( p );
						p->state = PEER_UNCONN;
					} else {
						list_add_tail_safe( &p->lh, &activ_peers );
						p->state = PEER_CONN;
					}
					lock_release( p->mutex );
					break;
				default:
					lock_release( p->mutex );
					error_code = 1;
					goto error;
			}
			break;
		case CER_RECEIVED:
			lock_get( p->mutex );
			switch (p->state) {
				case PEER_WAIT_CER:
					/* if peer in wait_cer timer list-> take it out */
					rmv_from_timer_list( &(p->tl), wait_cer_timer );
				case PEER_WAIT_CEA:
					DBG("DEBUG:peer_state_machine: CER received -> "
						"sending CEA\n");
					res = process_ce( p, (str*)ptr, 1 );
					/* send the response */
					if(send_cea( (str*)ptr, res, p)==-1 || res!=AAA_SUCCESS){
						tcp_close( p );
						reset_peer( p );
						p->state = PEER_UNCONN;
					} else {
						list_add_tail_safe( &p->lh, &activ_peers );
						p->state = PEER_CONN;
					}
					lock_release( p->mutex );
					break;
				default:
					lock_release( p->mutex );
					error_code = 1;
					goto error;
			}
			break;
		case DWR_RECEIVED:
			lock_get( p->mutex );
			switch (p->state) {
				case PEER_CONN:
				case PEER_WAIT_DWA:
				case PEER_WAIT_DPA:
					DBG("DEBUG:peer_state_machine: DWR received -> "
						"sending DWA\n");
					res = AAA_SUCCESS;
					if (process_dw( p, (str*)ptr, 1 )==-1)
						res = AAA_MISSING_AVP;
					/* send the response */
					if(send_dwa( (str*)ptr, res, p)==-1 || res!=AAA_SUCCESS){
						tcp_close( p );
						reset_peer( p );
						p->state = PEER_UNCONN;
					}
					lock_release( p->mutex );
					break;
				default:
					lock_release( p->mutex );
					error_code = 1;
					goto error;
			}
			break;
		case PEER_IS_INACTIV:
			lock_get( p->mutex );
			switch (p->state) {
				case PEER_CONN:
					if (p->last_activ_time+SEND_DWR_TIMEOUT<=get_ticks()) {
						if (send_dwr( p )==-1) {
							LOG(L_ERR,"ERROR:peer_state_machine: cannot send"
								" DWR -> trying later\n");
						} else {
							p->state = PEER_WAIT_DWA;
						}
					} else {
						DBG("DEBUG:peer_state_machine: sending DWR - false "
							"alarm -> cancel sending\n");
						list_add_tail_safe( &p->lh, &activ_peers );
					}
					lock_release( p->mutex );
					break;
				default:
					LOG(L_CRIT,"BUG:peer_state_machine: peer_is_inactiv "
						"triggered outside PEER_CONN state!\n");
					list_add_tail_safe( &p->lh, &activ_peers );
					lock_release( p->mutex );
					error_code = 1;
					goto error;
			}
			break;
		case DWA_RECEIVED:
			lock_get( p->mutex );
			switch (p->state) {
				case PEER_WAIT_DWA:
					process_dw( p, (str*)ptr, 0 );
					list_add_tail_safe( &p->lh, &activ_peers );
					p->state = PEER_CONN;
					lock_release( p->mutex );
					break;
				default:
					lock_release( p->mutex );
					error_code = 1;
					goto error;
			}
			break;
		case DPR_RECEIVED:
			lock_get( p->mutex );
			switch (p->state) {
				case PEER_CONN:
				case PEER_WAIT_DWA:
				case PEER_WAIT_DPA:
					list_del_safe( &p->lh , &activ_peers );
					DBG("DEBUG:peer_state_machine: DPR received -> "
						"sending DPA\n");
					res = AAA_SUCCESS;
					if (process_dp( p, (str*)ptr, 1 )==-1)
						res = AAA_MISSING_AVP;
					/* send the response */
					send_dpa( (str*)ptr, res, p);
					tcp_close( p );
					reset_peer( p );
					p->state = PEER_UNCONN;
					lock_release( p->mutex );
					break;
				default:
					lock_release( p->mutex );
					error_code = 1;
					goto error;
			}
			break;
		case DPA_RECEIVED:
			lock_get( p->mutex );
			switch (p->state) {
				case PEER_WAIT_DPA:
					tcp_close( p );
					reset_peer( p );
					p->state = PEER_UNCONN;
					lock_release( p->mutex );
					break;
				default:
					lock_release( p->mutex );
					error_code = 1;
					goto error;
			}
			break;
		case PEER_HANGUP:
			lock_get( p->mutex );
			switch (p->state) {
				case PEER_CONN:
					list_del_safe( &p->lh , &activ_peers );
					if (send_dpr( p ,(unsigned int)ptr )==-1) {
						LOG(L_ERR,"ERROR:peer_state_machine: cannot send"
							" DPR : closing tcp directly\n");
						tcp_close( p );
						reset_peer( p );
						p->state = PEER_UNCONN;
					} else {
						p->state = PEER_WAIT_DPA;
					}
					lock_release( p->mutex );
					break;
				default:
					lock_release( p->mutex );
					error_code = 1;
					goto error;
			}
			break;
		default:
			error_code = 2;
			goto error;
	}

	DBG("DEBUG:peer_state_machine: peer %p, new state = %d\n",
		p,p->state);

	return 1;
error:
	LOG(L_ERR,"ERROR:peer_state_machine: %s : peer=%p, state=%d, event=%d\n",
		err_msg[error_code],p,p->state,event);
	return -1;
}




void peer_timer_handler(unsigned int ticks, void* param)
{
	struct timer_link *tl;
	struct list_head  *lh;
	struct list_head  *foo;
	struct peer *p;

	/* RECONN TIME LIST */
	tl = get_expired_from_timer_list( reconn_timer, ticks);
	/* process the peers */
	while (tl) {
		p  = (struct peer*)tl->payload;
		tl = tl->next_tl;
		if (!p->flags&PEER_TO_DESTROY) {
			/* try again to connect the peer */
			DBG("DEBUG:peer_timer_handler: re-tring to connect peer %p \n",p);
			write_command( p->fd, TIMEOUT_PEER_CMD, PEER_RECONN_TIMEOUT, p, 0);
		}
	}

	/* WAIT_CER LIST */
	tl = get_expired_from_timer_list( wait_cer_timer, ticks);
	/* process the peers */
	while (tl) {
		p  = (struct peer*)tl->payload;
		tl = tl->next_tl;
		/* close the connection */
		DBG("DEBUG:peer_timer_handler: peer %p hasn't received CER yet!\n",p);
		write_command( p->fd, TIMEOUT_PEER_CMD, PEER_CER_TIMEOUT, p, 0);
	}

	/* ACTIV_PEERS LIST */
	if ( !list_empty(&(activ_peers.lh)) ) {
		lock_get( activ_peers.mutex );
		list_for_each_safe( lh, foo, &(activ_peers.lh)) {
			p  = list_entry( lh , struct peer , lh);
			if (p->last_activ_time+SEND_DWR_TIMEOUT<=ticks) {
				/* remove it from the list */
				list_del_zero( lh );
				/* send command to peer */
				write_command( p->fd, INACTIVITY_CMD, 0, p, 0);
			} else {
				break;
			}
		}
		lock_release( activ_peers.mutex );
	}
}

