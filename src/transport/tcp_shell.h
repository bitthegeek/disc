/*
 * $Id: tcp_shell.h,v 1.7 2003/04/22 19:58:41 andrei Exp $
 *
 *  History:
 *  --------
 *  2003-03-07  created by bogdan
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



#ifndef _TCP_SHELL_COMMON_H_
#define _TCP_SHELL_COMMON_H_

#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>
#include "../list.h"
#include "ip_addr.h"
#include "peer.h"



struct thread_info {
	/* linker into the peer list */
	struct list_head  tl;
	/* thread  id */
	pthread_t         tid;
	/* number of peer per thread */
	unsigned int      load;
	/* commad pipe */
	unsigned int      cmd_pipe[2];
	fd_set            rd_set;
	fd_set            wr_set;
};


struct command {
	unsigned int code;
	unsigned int fd;
	struct peer  *peer;
	void         *attrs;
};


enum COMMAND_CODES {
	SHUTDOWN_CMD,             /* (cmd,-,-,-) */
	START_ACCEPT_CMD,         /* (cmd,-,-,-) */
	CONNECT_CMD,              /* (cmd,-,peer,-) */
	ACCEPTED_CMD,             /* (cmd,accepted_fd,peer,-) */
	ADD_PEER_CMD,             /* (cmd,-,peer,-) */
	INACTIVITY_CMD,           /* (cmd,-,peer,-) */
	TIMEOUT_PEER_CMD,         /* (cmd,event,peer,-) */
	CLOSE_CMD,                /* (cmd,-,peer,-) */
};



#define COMMAND_SIZE (sizeof(struct command))



int init_tcp_shell(unsigned int nr_receivers);

void terminate_tcp_shell(void);

int get_new_receive_thread();

void start_tcp_accept();

void tcp_connect(struct peer *p);

void tcp_close( struct peer *p);

inline int static write_command(unsigned int fd_pipe, unsigned int code,
							unsigned int fd, struct peer *peer, void *attrs)
{
	struct command cmd;
	cmd.code  = code;
	cmd.fd    = fd;
	cmd.peer  = peer;
	cmd.attrs = attrs;
	return write( fd_pipe, &cmd, COMMAND_SIZE);
}

#endif

