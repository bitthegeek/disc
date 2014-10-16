/*
 * $Id: aaa_parse_uri.h,v 1.2 2003/04/22 19:58:41 andrei Exp $
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

/*
 * History:
 * --------
 *  2003-04-08  created by andrei
 */

#ifndef aaa_parse_uri_h
#define aaa_parse_uri_h


struct aaa_uri {
	str host;      /* Host name */
	str port;      /* Port number */
	str params;    /* Parameters, all of them */
	str transport; /* shortcut to transport */
	str protocol;  /* shortcut to protocol */
	unsigned short port_no;
};


int aaa_parse_uri(char* buf, int len, struct aaa_uri* uri);

#endif



