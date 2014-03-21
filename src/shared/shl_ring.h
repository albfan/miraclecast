/*
 * SHL - Ring buffer
 *
 * Copyright (c) 2011-2013 David Herrmann <dh.herrmann@gmail.com>
 * Dedicated to the Public Domain
 */

/*
 * Ring buffer
 */

#ifndef SHL_RING_H
#define SHL_RING_H

#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>

struct shl_ring {
	uint8_t *buf;		/* buffer or NULL */
	size_t size;		/* actual size of @buf */
	size_t start;		/* start position of ring */
	size_t used;		/* number of actually used bytes */
};

/* flush buffer so it is empty again */
void shl_ring_flush(struct shl_ring *r);

/* flush buffer, free allocated data and reset to initial state */
void shl_ring_clear(struct shl_ring *r);

/* get pointers to buffer data and their length */
size_t shl_ring_peek(struct shl_ring *r, struct iovec *vec);

/* copy data into external linear buffer */
size_t shl_ring_copy(struct shl_ring *r, void *buf, size_t size);

/* push data to the end of the buffer */
int shl_ring_push(struct shl_ring *r, const void *u8, size_t size);

/* pull data from the front of the buffer */
void shl_ring_pull(struct shl_ring *r, size_t size);

/* return size of occupied buffer in bytes */
static inline size_t shl_ring_get_size(struct shl_ring *r)
{
	return r->used;
}

#endif  /* SHL_RING_H */
