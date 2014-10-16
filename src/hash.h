#ifndef _HASH_H_
#define _HASH_H_

#include "list.h"
#include <stdarg.h>
#include <string.h>

struct hash_entry {
		void				*data;
		struct list_head	table[0];
};

typedef int (*hash_funct)(void *key);				// returns an integer identifying the bucket
typedef void *(*hash_get_key)(void *data);
typedef int (*hash_key_cmp)(void *data, void *key);


struct hash_table {
		hash_funct			hash_fn;
		hash_key_cmp		key_cmp;
		hash_get_key		get_key;
		int					size;
		int					idx;			/* index in group */
/*
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

		struct list_head	bucket_head[0];
};

struct hash_group {
	// allocation/deallocation functions
	int					tables_no;
	struct hash_table	*tables[0];			// chains multiple tables
};



#define HASH_TABLE(NAME, SIZE, ENTRY_TYPE, KEY_MEMBER, HASH_FN, HASH_KEY_CMP)\
\
struct {\
	struct hash_table;\
	struct list_head	padd[SIZE];\
} hash_table_##NAME;\
\
void *__hash_get_key_##NAME(void *data) {\
	if (data == NULL)\
		return NULL;\
	return (void *)(&((ENTRY_TYPE *)data)->KEY_MEMBER);\
}\
\
hash_funct		__hash_fn_##NAME = (hash_funct)HASH_FN;\
hash_key_cmp	__hash_key_cmp_##NAME = (hash_key_cmp)HASH_KEY_CMP; \
int				__hash_size_##NAME = SIZE; \
\
int init_hash_table_##NAME(void) { \
\
	int i; \
	((struct hash_table *)&hash_table_##NAME)->hash_fn =	__hash_fn_##NAME;\
	((struct hash_table *)&hash_table_##NAME)->key_cmp = __hash_key_cmp_##NAME;\
	((struct hash_table *)&hash_table_##NAME)->size = __hash_size_##NAME;\
\
	((struct hash_table *)&hash_table_##NAME)->idx = -1;\
	((struct hash_table *)&hash_table_##NAME)->get_key = __hash_get_key_##NAME;\
\
	for (i=0; i<__hash_size_##NAME; i++) {\
		INIT_LIST_HEAD( &((((struct hash_table *)&hash_table_##NAME)->bucket_head)[i]) );\
	}\
	return 0;\
};\
\
struct hash_entry *lookup_hash_group_##NAME(struct hash_group *g, void *k) {\
\
	struct hash_entry *e;\
	struct hash_table *t = (struct hash_table *)&hash_table_##NAME;\
	struct list_head *pos;\
\
	list_for_each( pos, &(t->bucket_head[t->hash_fn(k)]) ) {\
\
		printf("\tidx: %d\n", t->idx);\
		e = list_entry(pos, struct hash_entry, table[t->idx]);\
\
		if (t->key_cmp(e->data, k) == 0)\
			return e;\
	}\
	return NULL;\
};

int init_hash_group(struct hash_group **g, int n, ...) {

	va_list ap;
	int i;
	struct hash_table *t;

	va_start(ap, n);

	*g = (struct hash_group *)malloc(sizeof(struct hash_group) + n * sizeof(struct hash_table *));

	if (*g == NULL)
		return -1;
		
	(*g)->tables_no = n;

	for (i=0; i<n; i++) {
		t = va_arg(ap, struct hash_table *);
		if (t->idx != -1) {		/* hash table already included in a group */
			free(*g);
			return -1;
		}
		t->idx = i;
		(*g)->tables[i] = t;
	}
	va_end(ap);

	return 0;
};

/* chains a new hash entry into a hash group and the provided "data"
 * to the hash entry
 */
struct hash_entry *add_entry_hash_group(struct hash_group *g, void *data) {

	struct hash_entry	*e;
	struct list_head	*pos;
	struct hash_table	*t;
	int					i = 0, k;
		
	e = (struct hash_entry *)malloc(sizeof(struct hash_entry) + 
		g->tables_no * sizeof(struct list_head));

	for (i=0; i<g->tables_no; i++) {
		t = (g->tables)[i];
		k = t->hash_fn(t->get_key(data));
		printf("add_group_entry: bucket %d\n", k);
		k = k % t->size;		/* make sure it's within limits */
		list_add_tail(&e->table[i], &t->bucket_head[k] );
	}
	e->data = data;
	return e;
};

/* unchains a hash entry from a hash group and deallocates the hash entry
 * returns a pointer to the "data" field to be deallocated
 */
void *del_entry_hash_group(struct hash_group *g, struct hash_entry *e) {

	void *ret;
	int i;
		
	for (i=0; i<g->tables_no; i++) {
		list_del(&e->table[i]);
	}
	ret = e->data;
	free(e);
	return ret;
};

/* function prototype to clean up the "data" field */
typedef void (*cleanup_hash_entry)(void *);

/* unchains a hash entry from a hash group and deallocates the hash entry
 * calls "cln" function to deallocate the "data" field
 */
void cleanup_entry_hash_group(struct hash_group *g, struct hash_entry *e, 
				              cleanup_hash_entry cln) {
	if (cln)
		cln(del_entry_hash_group(g, e));
	else
		del_entry_hash_group(g, e);
};
		
#if 0
	void del_hash_group(struct hash_group *g) {

	struct hash_table *t;
	struct hash_entry *e;
	struct list_head *pos, *n;
	int i;
		
	t = g->tables[0];	/* it's enough to delete entries only once */
	for (i=0; i<t->size; i++) {
		list_for_each_safe(pos, n, &(t->bucket_head[i])) {
			e = list_entry(pos, struct hash_entry, table[0]);
			del_entry_hash_group(g, e);
		}
	}
};
#endif

void cleanup_hash_group(struct hash_group *g, cleanup_hash_entry cln) {

	struct hash_table *t;
	struct hash_entry *e;
	struct list_head *pos, *n;
	int i;
		
	t = g->tables[0];	/* it's enough to delete entries only once */
	for (i=0; i<t->size; i++) {
		list_for_each_safe(pos, n, &(t->bucket_head[i])) {
			e = list_entry(pos, struct hash_entry, table[0]);
			cleanup_entry_hash_group(g, e, cln);
		}
	}
	free(g);
};

typedef void (*print_funct)(void *data);

void print_hash_group(struct hash_group *g, print_funct print_fn) {

	struct hash_table *t;
	struct hash_bucket *b;
	struct list_head *pos;
	struct hash_entry *e;
	int i, j;
		
	for(i=0; i<g->tables_no; i++) {
		printf("==== TABLE %d ====\n", i);	
		t = g->tables[i];
		for(j=0; j<t->size; j++) {
			printf("==== BUCKET %d ====\n", j);
			list_for_each(pos, &(t->bucket_head)[j]) {
				e = list_entry(pos, struct hash_entry, table[i]);
				print_fn(e->data);
			}
		}
	}
};

#endif
