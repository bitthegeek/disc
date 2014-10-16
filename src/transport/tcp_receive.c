/*
 * $Id: tcp_receive.c,v 1.21 2003/04/22 19:58:41 andrei Exp $
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
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include "../diameter_msg/diameter_msg.h"
#include "../mem/shm_mem.h"
#include "../dprint.h"
#include "../list.h"
#include "../str.h"
#include "peer.h"
#include "ip_addr.h"
#include "tcp_receive.h"
#include "tcp_shell.h"


#define MAX_AAA_MSG_SIZE  65536



inline static int do_read( struct peer *p)
{
	unsigned char  *ptr;
	unsigned int   wanted_len, len;
	int n;
	str s;

	if (p->buf==0) {
		wanted_len = sizeof(p->first_4bytes) - p->buf_len;
		ptr = ((unsigned char*)&(p->first_4bytes)) + p->buf_len;
	} else {
		wanted_len = p->first_4bytes - p->buf_len;
		ptr = p->buf + p->buf_len;
	}

	while( (n=recv( p->sock, ptr, wanted_len, MSG_DONTWAIT ))>0 ) {
		DBG("DEBUG:do_read (sock=%d)  -> n=%d (expected=%d)\n",
			p->sock,n,wanted_len);
		p->buf_len += n;
		if (n<wanted_len) {
			//DBG("only %d bytes read from %d expected\n",n,wanted_len);
			wanted_len -= n;
			ptr += n;
		} else {
			if (p->buf==0) {
				/* I just finished reading the the first 4 bytes from msg */
				len = ntohl(p->first_4bytes)&0x00ffffff;
				if (len<AAA_MSG_HDR_SIZE || len>MAX_AAA_MSG_SIZE) {
					LOG(L_ERR,"ERROR:do_read (sock=%d): invalid message "
						"length read %u (%x)\n",p->sock,len,p->first_4bytes);
					goto error;
				}
				//DBG("message length = %d(%x)\n",len,len);
				if ( (p->buf=shm_malloc(len))==0  ) {
					LOG(L_ERR,"ERROR:do_read: no more free memory\n");
					goto error;
				}
				*((unsigned int*)p->buf) = p->first_4bytes;
				p->buf_len = sizeof(p->first_4bytes);
				p->first_4bytes = len;
				/* update the reading position and len */
				ptr = p->buf + p->buf_len;
				wanted_len = p->first_4bytes - p->buf_len;
			} else {
				/* I finished reading the whole message */
				DBG("DEBUG:do_read (sock=%d): whole message read (len=%d)!\n",
					p->sock,p->first_4bytes);
				s.s   = p->buf;
				s.len = p->buf_len;
				/* reset the read buffer */
				p->buf = 0;
				p->buf_len = 0;
				p->first_4bytes = 0;
				/* process the mesage */
				dispatch_message( p, &s);
				break;
			}
		}
	}

	//DBG(">>>>>>>>>> n=%d, errno=%d \n",n,errno);
	if (n==0) {
		LOG(L_INFO,"INFO:do_read (sock=%d): FIN received\n",p->sock);
		goto error;
	}
	if ( n==-1 && errno!=EINTR && errno!=EAGAIN ) {
		LOG(L_ERR,"ERROR:do_read (sock=%d): n=%d , errno=%d (%s)\n",
			p->sock, n, errno, strerror(errno));
		goto error;
	}

	return 1;
error:
	return -1;
}



inline void tcp_accept(struct peer *p, unsigned int s)
{
	struct tcp_params      info;
	struct ip_addr       local_ip;
	union sockaddr_union local;
	unsigned int length;
	unsigned int option;

	/* set the socket blocking */
	option = fcntl( s, F_GETFL, 0);
	fcntl( s, F_SETFL, option & ~O_NONBLOCK);
	/* set the socket NODELAY */
	option = 1;
	if (setsockopt(s,IPPROTO_TCP,TCP_NODELAY,&option,sizeof(option))==-1) {
		LOG(L_ERR,"ERROR:tcp_accept: setsockopt TCP_NODEALY failed: \"%s\"\n",
			strerror(errno));
		goto error;
	}

	/* get the address that the socket is connected to you */
	length = sockaddru_len(local);
	if (getsockname( s, (struct sockaddr *)&local, &length) == -1) {
		LOG(L_ERR,"ERROR:tcp_accept: getsocname failed: \"%s\"\n",
			strerror(errno));
		goto error;
	}

	su2ip_addr( &local_ip, &local );
	//DBG("-----> accepted on [%s]\n",ip_addr2a( &local_ip));
	info.sock  = s;
	info.local = &local_ip;
	/* call the peer state machine */
	if (peer_state_machine( p, TCP_ACCEPT, &info)==-1)
		goto error;

	FD_SET( s, &p->tinfo->rd_set);
	return;
error:
	close(s);
}




inline void do_connect( struct peer *p, int sock )
{
	union sockaddr_union local;
	struct ip_addr       local_ip;
	struct tcp_params      info;
	unsigned int         length;
	unsigned int         option;

	/* get the socket blocking */
	option = fcntl( sock, F_GETFL, 0);
	fcntl( sock, F_SETFL, option & ~O_NONBLOCK);
	/* set the socket NODELAY */
	option = 1;
	if (setsockopt(sock,IPPROTO_TCP,TCP_NODELAY,&option,sizeof(option))==-1) {
		LOG(L_ERR,"ERROR:tcp_connect: setsockopt TCP_NODEALY failed: \"%s\"\n",
			strerror(errno));
		peer_state_machine( p, TCP_CONN_FAILED,0);
		close( sock );
		return;

	}

	length = sockaddru_len(local);
	if (getsockname( sock, (struct sockaddr*)&local, &length)==-1) {
		LOG(L_ERR,"ERROR:do_conect: getsockname failed : %s\n",
			strerror(errno));
		peer_state_machine( p, TCP_CONN_FAILED,0);
		close( sock );
		return;
	}

	su2ip_addr( &local_ip, &local );
	info.sock = sock;
	info.local = &local_ip;
	if (peer_state_machine( p, TCP_CONNECTED, &info)==-1 ) {
		/* peer state machine didn't accepted this connect */
		close(sock);
	} else {
		/* start listening on this socket */
		FD_SET( sock, &(p->tinfo->rd_set));
	}
}




inline void tcp_connect(struct peer *p)
{
	union sockaddr_union remote;
	struct tcp_params      info;
	int option;
	int sock;

	sock = -1;

	/* create a new socket */
#ifdef USE_IPV6
	if (p->ip.af==AF_INET6)
		sock = socket(AF_INET6, SOCK_STREAM, 0);
	else
#endif
		sock = socket(AF_INET, SOCK_STREAM, 0);
	if ( sock==-1) {
		LOG(L_ERR,"ERROR:tcp_connect: cannot connect, failed to create "
				"new socket\n");
		goto error;
	}
	/* set connecting socket to non-blocking; after connection is done, the
	 * socket will be set back to blocking */
	option = fcntl(sock, F_GETFL, 0);
	fcntl(sock, F_SETFL, option | O_NONBLOCK);

	/* remote peer */
	if ( init_su( &remote, &(p->ip), p->port )==-1 )
		goto error;

	/* call connect non-blocking */
	if (connect(sock,(struct sockaddr*)&remote, sockaddru_len(remote))==0) {
		/* connection creation succeedes on the spot */
		do_connect(p, sock);
	} else {
		if (errno == EINPROGRESS) {
			/* connection creation performs asynch. */
			DBG("DEBUG:tcp_connect: connecting socket %d in progress\n",sock);
			info.sock = sock;
			if (peer_state_machine( p, TCP_CONN_IN_PROG, &info)==-1)
				close(sock);
			else
				FD_SET( sock, &(p->tinfo->wr_set) );
		} else {
			DBG("DEBUG:tcp_connect: connect failed : %s\n", strerror(errno));
			goto error;
		}
	}

	return;
error:
	if (sock!=-1)
		close(sock);
	/* generate connect failed */
	peer_state_machine( p, TCP_CONN_FAILED, 0);
}




void tcp_close( struct peer *p)
{
	DBG("DEBUG:tcp_close: closing socket %d\n",p->sock);
	close(p->sock);
	FD_CLR( p->sock, &p->tinfo->rd_set);
	FD_CLR( p->sock, &p->tinfo->wr_set);
}




void *do_receive(void *arg)
{
	struct list_head    *lh;
	struct list_head    peers;
	struct thread_info  *tinfo;
	struct peer         *p;
	fd_set ret_rd_set;
	fd_set ret_wr_set;
	int option;
	int length;
	int nready;
	int ncmd;
	struct command cmd;

	/* init section */
	tinfo = (struct thread_info*)arg;
	FD_ZERO( &(tinfo->rd_set) );
	FD_ZERO( &(tinfo->wr_set) );
	INIT_LIST_HEAD( &peers );

	/* add command pipe for listening */
	FD_SET( tinfo->cmd_pipe[0] , &tinfo->rd_set);

	while(1) {
		ret_rd_set = tinfo->rd_set;
		ret_wr_set = tinfo->wr_set;
		nready = select( FD_SETSIZE+1, &ret_rd_set, &ret_wr_set, 0, 0);

		if (nready == -1) {
			if (errno == EINTR) {
				continue;
			} else {
				LOG(L_ERR,"ERROR:do_receive: dispatcher call to select fails:"
					" %s\n", strerror(errno));
				sleep(2);
				continue;
			}
		}

		/*no timeout specified, therefore must never get into this if*/
		if (nready == 0)
			assert(0);

		if ( FD_ISSET( tinfo->cmd_pipe[0], &ret_rd_set) ) {
			/* read the command */
			ncmd = read( tinfo->cmd_pipe[0], &cmd, COMMAND_SIZE);
			if ( ncmd!=COMMAND_SIZE ){
				if (ncmd==0) {
					LOG(L_CRIT,"ERROR:do_receive: reading command "
						"pipe -> it's closed!!\n");
					goto error;
				}else if (ncmd<0) {
					LOG(L_ERR,"ERROR:do_receive: reading command "
						"pipe -> %s\n",strerror(errno));
				} else {
					LOG(L_ERR,"ERROR:do_receive: reading command "
						"pipe -> only %d\n",ncmd);
				}
			} else {
				/* execute the command */
				switch (cmd.code) {
					case SHUTDOWN_CMD:
						LOG(L_INFO,"INFO:do_receive: SHUTDOWN command "
							"received-> closing peer\n");
						//peer_state_machine( cmd.peer, PEER_HANGUP, 0);
						return 0;
						break;
					case ADD_PEER_CMD:
						LOG(L_INFO,"INFO:do_receive: adding new peer\n");
						list_add_tail( &cmd.peer->thd_peer_lh, &peers);
						cmd.peer->tinfo = tinfo;
					case CONNECT_CMD:
						LOG(L_INFO,"INFO:do_receive: connecting peer\n");
						tcp_connect( cmd.peer );
						break;
					case ACCEPTED_CMD:
						LOG(L_INFO,"INFO:do_receive: accept received\n");
						tcp_accept( cmd.peer , cmd.fd);
						break;
					case TIMEOUT_PEER_CMD:
						LOG(L_INFO,"INFO:do_receive: timeout received\n");
						peer_state_machine( cmd.peer, cmd.fd, cmd.attrs);
						break;
					case INACTIVITY_CMD:
						LOG(L_INFO,"INFO:do_receive: inactivity detected\n");
						peer_state_machine( cmd.peer, PEER_IS_INACTIV, 0);
						break;
					case CLOSE_CMD:
						LOG(L_INFO,"INFO:do_receive: close cmd. received\n");
						peer_state_machine( cmd.peer, TCP_CONN_CLOSE, 0);
						break;
					default:
						LOG(L_ERR,"ERROR:do_receive: unknown command "
							"code %d -> ignoring command\n",cmd.code);
				}
			}
			if (--nready==0)
				continue;
		}

		list_for_each( lh, &peers) {
			if (!nready)
				break;
			p = list_entry( lh, struct peer, thd_peer_lh);

			if ( p->sock!=-1 && FD_ISSET( p->sock, &ret_rd_set) ) {
				nready--;
				/* data received */
				if (do_read( p )==-1)
					peer_state_machine( p, TCP_CONN_CLOSE, 0);
				continue;
			}

			if ( p->sock!=-1 && FD_ISSET( p->sock, &ret_wr_set) ) {
				nready--;
				DBG("DEBUG:do_receive: connect done on socket %d\n",p->sock);
				FD_CLR( p->sock, &tinfo->wr_set);
				length = sizeof(option);
				if ((ncmd=getsockopt( p->sock, SOL_SOCKET, SO_ERROR, &option,
				&length))==-1 || option!=0 ) {
					LOG(L_ERR,"ERROR:do_receive: getsockopt=%d ; res=%d\n",
						ncmd, option);
					close( p->sock );
					peer_state_machine( p, TCP_CONN_FAILED, 0);
				} else {
					do_connect( p, p->sock);
				}
			}
		}

	}/*while*/

error:
	return 0;
}




