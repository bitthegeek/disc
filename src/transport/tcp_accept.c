/*
 * $Id: tcp_accept.c,v 1.10 2003/04/22 19:58:41 andrei Exp $
 *
 *  History:
 *  --------
 *  2003-03-07  created by bogdan
 *  2003-03-12  converted to shm_malloc/shm_free (andrei)
 *  2003-04-09  added disable_ipv6 support
 *              split do_accept into do_accept & accept_connection (andrei)
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
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <netinet/in.h>
#include "../dprint.h"
#include "../globals.h"
#include "peer.h"
#include "ip_addr.h"
#include "tcp_shell.h"
#include "tcp_accept.h"


/* used by do_accept
 * returns 0 on success, -1 on error */
inline static int accept_connection(int server_sock)
{
	unsigned int length;
	int accept_sock;
	union sockaddr_union remote;
	struct ip_addr remote_ip;
	struct peer  *peer;

		
	/* do accept */
	length = sizeof( union sockaddr_union);
	accept_sock = accept( server_sock, (struct sockaddr*)&remote, &length);
	if (accept_sock==-1) {
		/* accept failed */
		LOG(L_ERR,"ERROR:accept_connection: accept failed!\n");
		goto error;
	} else {
		LOG(L_INFO,"INFO:accept_connection: new tcp connection accepted!\n");
		/* lookup for the peer */
		su2ip_addr( &remote_ip, &remote);
		peer = lookup_peer_by_ip( &remote_ip );
		if (!peer) {
			LOG(L_ERR,"ERROR:accept_connection: connection from an"
					" unknown peer!\n");
			close( accept_sock );
			goto error;
		} else {
			/* generate ACCEPT_DONE command for the peer */
			write_command(peer->fd,ACCEPTED_CMD,accept_sock,peer,0);
		}
	}
	return 0;
error:
	return -1;
}


void *do_accept(void *arg)
{
	unsigned int server_sock4;
#ifdef USE_IPV6
	unsigned int server_sock6;
#endif
	unsigned int max_sock;
	struct thread_info *tinfo;
	struct command cmd;
	int  nready;
	int  ncmd;
	fd_set read_set;

	/* get the pointer with info about myself */
	tinfo = (struct thread_info*)arg;
	FD_ZERO( &(tinfo->rd_set) );

	/* add for listening the command pipe */
	FD_SET( tinfo->cmd_pipe[0], &(tinfo->rd_set));
	max_sock = tinfo->cmd_pipe[0];


	server_sock4 = -1;
	server_sock6 = -1;

	while(1){
		read_set = tinfo->rd_set;
		nready = select( max_sock+1, &read_set, 0, 0, 0);
		if (nready == -1) {
			if (errno == EINTR) {
				continue;
			} else {
				LOG(L_ERR,"ERROR:do_accept: select fails: %s\n",
					strerror(errno));
				sleep(2);
				continue;
			}
		}

		/*no timeout specified, therefore must never get into this if*/
		if (nready == 0) 
			assert(0);
#ifdef USE_IPV6
		if (nready && (!disable_ipv6)&&(server_sock6!=-1)&&
		(FD_ISSET(server_sock6, &read_set))){
				accept_connection(server_sock6);
				nready--;
		}
#endif
		if (nready && (server_sock4!=-1)&&(FD_ISSET(server_sock4, &read_set))){
				accept_connection(server_sock4);
				nready--;
		}
		if (nready && FD_ISSET( tinfo->cmd_pipe[0], &read_set) ) {
			/* read the command */
			if ((ncmd=read( tinfo->cmd_pipe[0], &cmd, COMMAND_SIZE)) !=
					COMMAND_SIZE) {
				if (ncmd==0) {
					LOG(L_CRIT,"ERROR:do_accept: reading command "
						"pipe -> it's closed!!\n");
					goto error;
				}else if (ncmd<0) {
					LOG(L_ERR,"ERROR:do_accept: reading command "
						"pipe -> %s\n",strerror(errno));
					continue;
				} else {
					LOG(L_ERR,"ERROR:do_accept: reading command "
						"pipe -> only %d\n",ncmd);
					continue;
				}
			}
			/* execute the command */
			switch (cmd.code) {
				case SHUTDOWN_CMD:
					LOG(L_INFO,"INFO:do_accept: SHUTDOWN command "
						"received-> exiting\n");
					return 0;
					break;
				case START_ACCEPT_CMD:
					LOG(L_INFO,"INFO:do_accept: START_ACCEPT command "
						"received-> start accepting connections\n");
#ifdef USE_IPV6
					if (!disable_ipv6) {
						server_sock6 = ((unsigned int*)(cmd.attrs))[1];
						DBG("DEBUG:do_accept: adding server_sock6\n");
						/* update the max_sock */
						if (server_sock6>max_sock)
							max_sock = server_sock6;
						FD_SET( server_sock6, &(tinfo->rd_set));
					}
#endif
					server_sock4 = ((unsigned int*)(cmd.attrs))[0];
					if (server_sock4!=-1) {
						DBG("DEBUG:do_accept: adding server_sock4\n");
						/* update the max_sock */
						if (server_sock4>max_sock)
							max_sock = server_sock4;
						FD_SET( server_sock4, &(tinfo->rd_set));
					}
					break;
				default:
					LOG(L_ERR,"ERROR:do_accept: unknown command "
						"code %d -> ignoring command\n",cmd.code);
			}
		}
	}
error:
	kill( 0, SIGINT );
	return 0;
}

