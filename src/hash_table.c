/*
 * $Id: hash_table.c,v 1.7 2003/04/22 19:58:41 andrei Exp $
 *
 * 2003-01-29  created by bogdan
 * 2003-03-12  converted to use shm_malloc (andrei)
 * 2003-03-13  converted to locking.h /gen_lock_t (andrei)
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


#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "mem/shm_mem.h"
#include "dprint.h"
#include "list.h"
#include "str.h"
#include "aaa_lock.h"
#include "hash_table.h"



/*
 * Builds and inits the hash table; Size of table is given by param
 */
struct h_table* build_htable( unsigned int hash_size)
{
	struct h_table *table;
	int            i;

	/* inits */
	table = 0;

	/* allocates the hash table */
	table = (struct h_table*)shm_malloc( sizeof(struct h_table));
	if (!table) {
		LOG(L_CRIT,"ERROR:build_htable: cannot get free memory!\n");
		goto error;
	}
	memset( table, 0, sizeof(struct h_table));
	table->hash_size = hash_size;

	/* allocates an array of hash entrys */
	table->entrys = (struct h_entry*)shm_malloc( hash_size*
													sizeof(struct h_entry));
	if (!table->entrys) {
		LOG(L_CRIT,"ERROR:build_htable: cannot get free memory!\n");
		goto error;
	}
	memset( table->entrys, 0, hash_size*sizeof(struct h_entry));

	/* create the mutex for it */
	table->mutex = create_locks( 1 );
	if (!table->mutex) {
		LOG(L_CRIT,"ERROR:build_htable: cannot create mutex lock!\n");
		goto error;
	}

	/* init the label counter and linked list head */
	for(i=0;i<hash_size;i++) {
		table->entrys[i].next_label  = ((unsigned int)time(0))<<20;
		table->entrys[i].next_label |= ((unsigned int)rand( ))>>12;
		INIT_LIST_HEAD( &(table->entrys[i].lh) );
	}

	return table;
error:
	return 0;
}



/*
 * Destroy the hash table: mutex are released and memory is freed
 */
void destroy_htable( struct h_table *table)
{
	if (table) {
		/* deal with the entryes */
		if (table->entrys) {
			/* free the entry's array */
			shm_free( table->entrys  );
		}
		/* destroy the mutex */
		if (table->mutex)
			destroy_locks( table->mutex, 1);
		/* at last, free the table structure */
		shm_free( table);
	}
}



/*
 * Adds a cell into the hash table on the apropriate hash entry
 * ( it uses h_link->hash_code); also the label (unique identifier into the
 * entry link list) is set.
 */
int add_cell_to_htable( struct h_table *table, struct h_link *link)
{
	struct h_entry *entry;

	if (link->hash_code<0 || link->hash_code>table->hash_size-1 )
		return -1;

	entry = &(table->entrys[link->hash_code]);

	lock_get( table->mutex );
	/* get a label for this session */
	link->label = entry->next_label++;
	/* insert the session into the linked list at the end */
	list_add_tail( &(link->lh), &(entry->lh) );
	lock_release( table->mutex );

	return 1;
}



/*
 * Removes a cell from the hash_table
 */
int remove_cell_from_htable(struct h_table *table, struct h_link *link)
{
	lock_get( table->mutex );
	if (link->lh.next==0 || link->lh.prev==0) {
		LOG(L_WARN,"WARNING:remove_cell_from_htable: removing a cell already "
			"removed\n");
		lock_release( table->mutex );
		return -1;
	}
	list_del_zero( &(link->lh) );
	lock_release( table->mutex );
	return 1;
}



/*
 * Based on a string, calculates a hash code.
 */
int hash( str *s, unsigned int hash_size )
{
#define h_inc h+=v^(v>>3)
	char* p;
	register unsigned v;
	register unsigned h;

	h=0;
	for (p=s->s; p<=(s->s+s->len-4); p+=4){
		v=(*p<<24)+(p[1]<<16)+(p[2]<<8)+p[3];
		h_inc;
	}
	v=0;
	for (;p<(s->s+s->len); p++){ v<<=8; v+=*p;}
	h_inc;

	h=((h)+(h>>11))+((h>>13)+(h>>23));
	return (h)&(hash_size-1);
#undef h_inc
}



