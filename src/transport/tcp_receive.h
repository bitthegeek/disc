/*
 * $Id: tcp_receive.h,v 1.3 2003/04/22 19:58:41 andrei Exp $
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




#ifndef _TCP_SHELL_RECEIVE_H
#define _TCP_SHELL_RECEIVE_H



#include <sys/types.h>
#include <sys/select.h>


void* do_receive(void *attr);


#endif


