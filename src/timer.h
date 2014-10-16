/*
 * $Id: timer.h,v 1.6 2003/04/22 19:58:41 andrei Exp $
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




#ifndef _AAA_DIAMETER_TIMER_H
#define _AAA_DIAMETER_TIMER_H

#include "aaa_lock.h"
#include "locking.h"


#define TIMER_TICK 1
#define is_in_timer_list(_tl)  ( (_tl)->timer_list )
#define get_ticks()            (jiffies)


typedef void (timer_function)(unsigned int ticks, void* param);


/* */
struct timer_handler{
	timer_function* timer_f;
	void* t_param;
	unsigned int interval;
	unsigned int expires;
	struct timer_handler* next;
};


/* all you need to put a cell in a timer list
   links to neighbours and timer value */
typedef struct timer_link
{
	struct timer_link *next_tl;
	struct timer_link *prev_tl;
	volatile unsigned int timeout;
	void              *payload;
	struct timer      *timer_list;
}timer_link_type ;


/* timer list: includes head, tail and protection semaphore */
typedef struct timer
{
	struct timer_link  first_tl;
	struct timer_link  last_tl;
	gen_lock_t*          mutex;
} timer_type;


/* global time */
extern int jiffies;



/************************* Functions to manipulate the timer *****************/

/*register a periodic timer;
 * ret: <0 on errror*/
int register_timer(timer_function f, void* param, unsigned int interval);

void timer_ticker();

int destroy_timer();


/******************** Functions to manipulate the timer lists ****************/

struct timer* new_timer_list();

void destroy_timer_list( struct timer* timer_list );

int add_to_timer_list( struct timer_link*, struct timer* ,unsigned int);

int insert_into_timer_list( struct timer_link*, struct timer* ,unsigned int);

int rmv_from_timer_list( struct timer_link*, struct timer* t_list  );

struct timer_link* get_expired_from_timer_list( struct timer*, unsigned int );


#endif
