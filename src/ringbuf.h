#include <string.h>
#include <stdlib.h>

typedef struct {
	float* data;
	size_t rp;
	size_t wp;
	size_t len;
} ringbuf;

static ringbuf* rb_alloc (size_t siz) {
	ringbuf* rb = (ringbuf*) malloc (sizeof (ringbuf));
	rb->data = (float*) malloc (siz * sizeof(float));
	rb->len = siz;
	rb->rp = 0;
	rb->wp = 0;
	return rb;
}

static void rb_free(ringbuf *rb) {
	free(rb->data);
	free(rb);
}

static size_t rb_write_space (ringbuf* rb) {
	if (rb->rp == rb->wp) {
		return (rb->len - 1);
	}
	return ((rb->len + rb->rp - rb->wp) % rb->len) -1;
}

static size_t rb_read_space (ringbuf* rb) {
	return ((rb->len + rb->wp - rb->rp) % rb->len);
}

static int rb_read_one (ringbuf* rb, float* data) {
	if (rb_read_space (rb) < 1) {
		return -1;
	}
	*data = rb->data[rb->rp];
	rb->rp = (rb->rp + 1) % rb->len;
	return 0;
}

static ssize_t rb_read (ringbuf* rb, float* data, size_t len) {
	if (rb_read_space(rb) < len) {
		return -1;
	}
	if (rb->rp + len <= rb->len) {
		memcpy ((void*)data, (void*)&rb->data[rb->rp], len * sizeof (float));
	} else {
		const size_t part = rb->len - rb->rp;
		const size_t remn = len - part;
		memcpy ((void*) data,        (void*) &rb->data[rb->rp], part * sizeof (float));
		memcpy ((void*) &data[part], (void*) rb->data,          remn * sizeof (float));
	}
	rb->rp = (rb->rp + len) % rb->len;
	return 0;
}

static int rb_write (ringbuf* rb, const float* data, size_t len) {
	if (rb_write_space(rb) < len) {
		return -1;
	}
	if (rb->wp + len <= rb->len) {
		memcpy ((void*) &rb->data[rb->wp], (void*) data, len * sizeof (float));
	} else {
		int part = rb->len - rb->wp;
		int remn = len - part;
		memcpy ((void*) &rb->data[rb->wp], (void*) data,        part * sizeof (float));
		memcpy ((void*) rb->data,          (void*) &data[part], remn * sizeof (float));
	}
	rb->wp = (rb->wp + len) % rb->len;
	return 0;
}

static void rb_read_clear(ringbuf *rb) {
	rb->rp = rb->wp;
}
