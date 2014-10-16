/*
 * $Id: aaa_lock.h,v 1.3 2003/04/22 19:58:41 andrei Exp $
 *
 * 2003-01-29 created by bogdan
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


#ifndef _AAA_DIAMETER_AAA_LOCK_H
#define _AAA_DIAMETER_AAA_LOCK_H

#include "locking.h"


gen_lock_t* create_locks(int n);

void destroy_locks( gen_lock_t *locks, int n);

#endif
