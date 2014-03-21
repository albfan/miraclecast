/*
 * SHL - Double Linked List
 *
 * Copyright (c) 2010-2013 David Herrmann <dh.herrmann@gmail.com>
 * Dedicated to the Public Domain
 */

/*
 * A simple double linked list implementation
 * This list API does not provide type-safety! It is a simple circular
 * double-linked list. Objects need to embed "struct shl_dlist". This is used to
 * link and iterate a list. You can get the object back from a shl_dlist pointer
 * via shl_dlist_entry(). This breaks any type-safety, though. You need to make
 * sure you call this on the right objects.
 */

#ifndef SHL_DLIST_H
#define SHL_DLIST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include "shl_macro.h"

/* double linked list */

struct shl_dlist {
	struct shl_dlist *next;
	struct shl_dlist *prev;
};

#define SHL_DLIST_INIT(head) { &(head), &(head) }

static inline void shl_dlist_init(struct shl_dlist *list)
{
	list->next = list;
	list->prev = list;
}

static inline void shl_dlist__link(struct shl_dlist *prev,
					struct shl_dlist *next,
					struct shl_dlist *n)
{
	next->prev = n;
	n->next = next;
	n->prev = prev;
	prev->next = n;
}

static inline void shl_dlist_link(struct shl_dlist *head,
					struct shl_dlist *n)
{
	return shl_dlist__link(head, head->next, n);
}

static inline void shl_dlist_link_tail(struct shl_dlist *head,
					struct shl_dlist *n)
{
	return shl_dlist__link(head->prev, head, n);
}

static inline void shl_dlist__unlink(struct shl_dlist *prev,
					struct shl_dlist *next)
{
	next->prev = prev;
	prev->next = next;
}

static inline void shl_dlist_unlink(struct shl_dlist *e)
{
	if (e->prev && e->next) {
		shl_dlist__unlink(e->prev, e->next);
		e->prev = NULL;
		e->next = NULL;
	}
}

static inline bool shl_dlist_linked(struct shl_dlist *e)
{
	return e->next && e->prev;
}

static inline bool shl_dlist_empty(struct shl_dlist *head)
{
	return head->next == head;
}

static inline struct shl_dlist *shl_dlist_first(struct shl_dlist *head)
{
	return head->next;
}

static inline struct shl_dlist *shl_dlist_last(struct shl_dlist *head)
{
	return head->prev;
}

#define shl_dlist_entry(ptr, type, member) \
	shl_container_of((ptr), type, member)

#define shl_dlist_first_entry(head, type, member) \
	shl_dlist_entry(shl_dlist_first(head), type, member)

#define shl_dlist_last_entry(head, type, member) \
	shl_dlist_entry(shl_dlist_last(head), type, member)

#define shl_dlist_for_each(iter, head) \
	for (iter = (head)->next; iter != (head); iter = iter->next)

#define shl_dlist_for_each_but_one(iter, start, head) \
	for (iter = ((start)->next == (head)) ? \
				(start)->next->next : \
				(start)->next; \
	     iter != (start); \
	     iter = (iter->next == (head) && (start) != (head)) ? \
				iter->next->next : \
				iter->next)

#define shl_dlist_for_each_safe(iter, tmp, head) \
	for (iter = (head)->next, tmp = iter->next; iter != (head); \
		iter = tmp, tmp = iter->next)

#define shl_dlist_for_each_reverse(iter, head) \
	for (iter = (head)->prev; iter != (head); iter = iter->prev)

#define shl_dlist_for_each_reverse_but_one(iter, start, head) \
	for (iter = ((start)->prev == (head)) ? \
				(start)->prev->prev : \
				(start)->prev; \
	     iter != (start); \
	     iter = (iter->prev == (head) && (start) != (head)) ? \
				iter->prev->prev : \
				iter->prev)

#define shl_dlist_for_each_reverse_safe(iter, tmp, head) \
	for (iter = (head)->prev, tmp = iter->prev; iter != (head); \
		iter = tmp, tmp = iter->prev)

#endif /* SHL_DLIST_H */
