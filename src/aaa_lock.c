/*
 * $Id: aaa_lock.c,v 1.4 2003/04/22 19:58:41 andrei Exp $
 *
 * 2003-01-29  created by bogdan
 * 2003-03-12  converted to shm_malloc/shm_free (andrei)
 * 2003-03-13  converted to locking.h/gen_lock_t (andrei)
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
#include "aaa_lock.h"

#include "mem/shm_mem.h"


gen_lock_t* create_locks(int n)
{
	gen_lock_t  *locks;
	int       i;

	/* alocate the mutex variables */
	locks = 0;
	locks = (gen_lock_t*)shm_malloc( n * sizeof(gen_lock_t) );
	if (locks==0)
		goto error;

	/* init the mutexs */
	for(i=0;i<n;i++)
		if (lock_init( &locks[i])==0)
			goto error;

	return locks;
error:
	if (locks) shm_free((void*)locks);
	return 0;
}



void destroy_locks( gen_lock_t *locks, int n)
{
	int i;

	/* destroy the mutexs */
	if (locks){
		for(i=0;i<n;i++)
			lock_destroy( &locks[i] );

		/* free the memory zone */
		if (locks) shm_free( (void*)locks );
	}
}
