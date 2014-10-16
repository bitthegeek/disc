/*
 * $Id: tcp_shell.c,v 1.11 2003/04/22 21:18:44 bogdan Exp $
 *
 *  History:
 *  --------
 *  2003-03-07  created by bogdan
 *  2003-03-12  converted to shm_malloc/shm_free (andrei)
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
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include "../list.h"
#include "../dprint.h"
#include "../globals.h"
#include "../aaa_lock.h"
#include "../locking.h"
#include "ip_addr.h"
#include "peer.h"
#include "tcp_accept.h"
#include "tcp_receive.h"
#include "tcp_shell.h"


#define ADDRESS_ALREADY_IN_USE 98

#define ACCEPT_THREAD_ID      0
#define RECEIVE_THREAD_ID(_n) (1+(_n))


/* array with all the threads created by the tcp_shell */
static struct thread_info  *tinfo = 0;
static unsigned int        nr_recv_threads;
/* linked list with the receiving threads ordered by load */
static struct list_head    rcv_thread_list;
static gen_lock_t          *list_mutex = 0;
#ifdef USE_IPV6
static unsigned int listen_socks[2] = {-1,-1};
#else
static unsigned int listen_socks[1] = {-1};
#endif


#define get_payload( _pos ) \
		list_entry( (_pos), struct thread_info, tl )



int create_socks(unsigned int *socks)
{
	struct ip_addr servip;
	union sockaddr_union servaddr;
	unsigned int server_sock4;
#ifdef USE_IPV6
	unsigned int server_sock6;
	unsigned int bind_retest;
#endif
	unsigned int option;


	server_sock4 = -1;
#ifdef USE_IPV6
	server_sock6 = -1;
	bind_retest = 0;
	if (disable_ipv6) goto do_bind4; /* skip ipv6 binding part*/
do_bind6:
	LOG(L_INFO,"INFO:init_tcp_shell: doing socket and bind for IPv6...\n");
	/* create the listening socket fpr IPv6 */
	if ((server_sock6 = socket(AF_INET6, SOCK_STREAM, 0)) == -1) {
		LOG(L_ERR,"ERROR:init_tcp_shell: error creating server socket IPv6:"
			" %s\n",strerror(errno));
		goto error;
	}

	// TO DO: what was this one good for?
	option = 1;
	setsockopt(server_sock6,SOL_SOCKET,SO_REUSEADDR,&option,sizeof(option));

	memset( &servip, 0, sizeof(servip) );
	servip.af = AF_INET6;
	servip.len = 16;
	memcpy( &servip.u.addr, &in6addr_any, servip.len);
	init_su( &servaddr, &servip, htons(listen_port));

	if ( (bind( server_sock6, (struct sockaddr*)&servaddr,
	sockaddru_len(servaddr)))==-1) {
		LOG(L_ERR,"ERROR:init_tcp_shell: error binding server socket IPv6:"
			" %s\n",strerror(errno));
		goto error;
	}

	/* IMPORTANT: I have to do listen here to be able to ketch an AAIU on IPv4;
	 * bind is not sufficient because of the SO_REUSEADDR option */
	if ((listen( server_sock6, 4)) == -1) {
		LOG(L_ERR,"ERROR:init_tcp_shell: error listening on server socket "
			"IPv6: %s\n",strerror(errno));
		goto error;
	}

	if (bind_retest)
		goto bind_done;

do_bind4:
#endif
	LOG(L_INFO,"INFO:init_tcp_shell: doing socket and bind for IPv4...\n");
	/* create the listening socket fpr IPv4 */
	if ((server_sock4 = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		LOG(L_ERR,"ERROR:init_tcp_shell: error creating server socket IPv4:"
			" %s\n",strerror(errno));
		goto error;
	}

	// TO DO: what was this one good for?
	option = 1;
	setsockopt(server_sock4,SOL_SOCKET,SO_REUSEADDR,&option,sizeof(option));

	memset( &servip, 0, sizeof(servip) );
	servip.af = AF_INET;
	servip.len = 4;
	servip.u.addr32[0] = INADDR_ANY;
	init_su( &servaddr, &servip, htons(listen_port));

	if ((bind( server_sock4, (struct sockaddr*)&servaddr,
	sockaddru_len(servaddr)))==-1 ) {
#ifdef USE_IPV6
		if (!disable_ipv6){
			if (errno==ADDRESS_ALREADY_IN_USE && !bind_retest) {
				/* I'm doing bind4 for the first time */
				LOG(L_WARN,"WARNING:init_tcp_shell: got AAIU for IPv4 with"
						" IPv6 on -> close IPv6 and retry\n");
				close( server_sock6 );
				server_sock6 = -1;
				bind_retest = 1;
				goto do_bind4;
			}
		}
#endif
		LOG(L_ERR,"ERROR:init_tcp_shell: error binding server socket IPv4:"
			" %s\n",strerror(errno));
		goto error;
	}
#ifdef USE_IPV6
	else {
		if (!disable_ipv6){
			if (bind_retest) {
				/* if a manage to re-bind4 (after an AAIU) with bind6 disable,
				 * means the OS does automaticlly bind4 when bind6; in this 
				 * case I will disable the bind4 and re-bind6 */
				LOG(L_INFO,"INFO:init_tcp_shell: IPv4 bind succeded on re-"
					"testing without IPv6 -> close IPv4 and bind only IPv6\n");
				close( server_sock4 );
				server_sock4 = -1;
				goto do_bind6;
			} else {
				LOG(L_INFO,"INFO:init_tcp_shell: bind for IPv4 succeded along"
					" with bind IPv6 -> keep them both\n");
			}
		}
	}
#endif

	/* binding part done -> do listen */

#ifdef USE_IPV6
bind_done:

	if (server_sock4!=-1) {
#endif
	/* IPv4 sock */
	if ((listen( server_sock4, 4)) == -1) {
		LOG(L_ERR,"ERROR:init_tcp_shell: error listening on server socket "
			"IPv4: %s\n",strerror(errno) );
		goto error;
	}
#ifdef USE_IPV6
	}
#endif

	/* success */
	socks[0] = server_sock4;
#ifdef USE_IPV6
	socks[1] = server_sock6;
#endif

	return 1;
error:
	if (server_sock4!=-1) close(server_sock4);
#ifdef USE_IPV6
	if (server_sock6!=-1) close(server_sock6);
#endif
	return -1;
}




int init_tcp_shell( unsigned int nr_receivers)
{
	unsigned int i;

	tinfo = (struct thread_info*)shm_malloc
		( (nr_receivers+1)*sizeof(struct thread_info) );
	if (!tinfo) {
		LOG(L_ERR,"ERRROR:init_tcp_shell: no more free memory\n");
		goto error;
	}
	memset(tinfo, 0, sizeof((nr_receivers+1)*sizeof(struct thread_info)));
	nr_recv_threads = nr_receivers;

	/* prepare the list for receive threads */
	INIT_LIST_HEAD(  &rcv_thread_list );
	list_mutex = create_locks( 1 );
	if (!list_mutex) {
		LOG(L_ERR,"ERROR:init_tcp_shell: cannot create mutex!\n");
		goto error;
	}

	/* create the sockets, do bindd and listen on them */
	if ( create_socks( listen_socks )==-1 ) {
		LOG(L_CRIT,"ERROR:init_tcp_shell: unable to create sockets\n");
		goto error;
	}

	/* build the threads */
	/* accept thread */
	if (pipe( tinfo[ACCEPT_THREAD_ID].cmd_pipe )!=0) {
		LOG(L_ERR,"ERROR:init_tcp_shell: cannot create pipe for accept "
			"thread : %s\n", strerror(errno));
		goto error;
	}
	if (pthread_create( &tinfo[ACCEPT_THREAD_ID].tid, 0/*&attr*/,
	&do_accept, &tinfo[ACCEPT_THREAD_ID])!=0) {
		LOG(L_ERR,"ERROR:init_tcp_shell: cannot create dispatcher thread\n");
		goto error;
	}

	/* receive threads */
	for(i=0; i<nr_recv_threads; i++) {
		if (pipe( tinfo[RECEIVE_THREAD_ID(i)].cmd_pipe )!=0) {
			LOG(L_ERR,"ERROR:init_tcp_shell: cannot create pipe for receive "
				"thread %d : %s\n", i,strerror(errno));
			goto error;
		}
		if (pthread_create( &tinfo[RECEIVE_THREAD_ID(i)].tid, 0/*&attr*/ ,
		&do_receive, &tinfo[RECEIVE_THREAD_ID(i)])!=0 ) {
			LOG(L_ERR,"ERROR:init_tcp_shell: cannot create receive thread\n");
			goto error;
		}
		list_add_tail( &(tinfo[RECEIVE_THREAD_ID(i)].tl), &rcv_thread_list);
	}

	LOG(L_INFO,"INFO:init_tcp_shell: tcp shell started\n");
	return 1;
error:
	return -1;
}



void terminate_tcp_shell()
{
	struct command cmd;
	int i;

	if (tinfo==0)
		return;

	/* build a  SHUTDOWN command */
	memset( &cmd, 0, COMMAND_SIZE);
	cmd.code = SHUTDOWN_CMD;

	/* send it to the accept thread */
	if (tinfo[ACCEPT_THREAD_ID].tid)
		write( tinfo[ACCEPT_THREAD_ID].cmd_pipe[1], &cmd, COMMAND_SIZE);
	else
		LOG(L_INFO,"INFO:terminate_tcp_shell: accept thread not created\n");

	/* ... and to the receiver threads */
	for(i=0; i<nr_recv_threads; i++)
		if (tinfo[RECEIVE_THREAD_ID(i)].tid)
			write(tinfo[RECEIVE_THREAD_ID(i)].cmd_pipe[1],&cmd,COMMAND_SIZE);
		else
			LOG(L_INFO,"INFO:terminate_tcp_shell: receive thread %d not "
				"created\n",i);

	/* now wait for them to end */
	/* accept thread */
	if (tinfo[ACCEPT_THREAD_ID].tid)
		pthread_join( tinfo[ACCEPT_THREAD_ID].tid, 0);
	LOG(L_INFO,"INFO:terminate_tcp_shell: accept thread terminated\n");

	/* close the sockets */
	if (listen_socks[0]) close(listen_socks[0]);
#ifdef USE_IPV6
	if (listen_socks[1]) close(listen_socks[1]);
#endif

	/* receive threads */
	for(i=0; i<nr_recv_threads; i++) {
		if (tinfo[RECEIVE_THREAD_ID(i)].tid) {
			pthread_join( tinfo[RECEIVE_THREAD_ID(i)].tid, 0);
			LOG(L_INFO,"INFO:terminate_tcp_shell: receive thread %d "
				"terminated\n",i);
		}
	}

	/* destroy the lock list */
	if (list_mutex)
		destroy_locks( list_mutex, 1);

	/* free the thread's info array */
	shm_free( tinfo );

	LOG(L_INFO,"INFO:terminate_tcp_shell: tcp shell stoped\n");
}



void start_tcp_accept()
{
	struct command cmd;

	/* build a START command */
	memset( &cmd, 0, COMMAND_SIZE);
	cmd.code  = START_ACCEPT_CMD;
	cmd.attrs = listen_socks;

	/* start the accept thread */
	write( tinfo[ACCEPT_THREAD_ID].cmd_pipe[1], &cmd, COMMAND_SIZE );

}



int get_new_receive_thread()
{
	struct thread_info *ti;
	struct list_head   *pos;

	/* lock the list */
	lock_get( list_mutex );

	/* get the first element from list */
	ti = get_payload( rcv_thread_list.next );
	/* increase its load */
	ti->load++;

	if ( ti->tl.next!=&rcv_thread_list &&
	get_payload(ti->tl.next)->load<ti->load ){
		/* remove the first element */
		list_del_zero( rcv_thread_list.next );
		/* put it back in list into the corect position */
		list_for_each( pos, &rcv_thread_list ) {
			if ( get_payload(pos)->load==ti->load ) {
				list_add_tail( &(ti->tl), pos);
				break;
			}
		/* add at the end */
		list_add_tail( &(ti->tl), &rcv_thread_list);
		}
	}
	/* unlock the list */
	lock_release( list_mutex );

	LOG(L_INFO,"INFO:get_new_receive_thread: returning thread %p load[%d]\n",
		ti,ti->load);
	return ti->cmd_pipe[1];
}



void release_peer_thread()
{
	/* lock the list */
	lock_get( list_mutex );

	//.......................

	/* unlock the list */
	lock_release( list_mutex );
}



#undef get_payload


