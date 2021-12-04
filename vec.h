#ifndef VEC_TYPE
	#error NO VEC_TYPE
#endif

#ifndef VEC_NAME
	#define VEC_NAME VEC_TYPE
#endif

#ifndef VEC_INIT_SIZE
	#define VEC_INIT_SIZE 16
#endif

#ifndef VEC_OOM_HANDLER
	#include <stdio.h>
	#define VEC_OOM_HANDLER fprintf(stderr,"out of memory\n");exit(1)
#endif

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define VEC_CONCAT_INNER(a, b) a ## _ ## b
#define VEC_CONCAT(a, b) VEC_CONCAT_INNER(a, b)
#define VEC_SUFFIX(a) VEC_CONCAT(vec, VEC_CONCAT(VEC_NAME, a))
#define VEC_TYPE_T VEC_SUFFIX(t)

typedef struct {
	VEC_TYPE *data;
	size_t count, cap;
} VEC_TYPE_T;

void VEC_SUFFIX(expand_to)(VEC_TYPE_T *vec, size_t count);
void VEC_SUFFIX(shrink)(VEC_TYPE_T *vec);
void VEC_SUFFIX(expand_by)(VEC_TYPE_T *vec, size_t count);
void VEC_SUFFIX(push)(VEC_TYPE_T *vec, VEC_TYPE elem);
void VEC_SUFFIX(push_arr)(VEC_TYPE_T *vec, const VEC_TYPE *arr, size_t count);
VEC_TYPE VEC_SUFFIX(pop)(VEC_TYPE_T *vec);
void VEC_SUFFIX(clear)(VEC_TYPE_T *vec);
VEC_TYPE VEC_SUFFIX(get)(VEC_TYPE_T *vec, size_t i);
size_t VEC_SUFFIX(count)(VEC_TYPE_T *vec);
void VEC_SUFFIX(free)(VEC_TYPE_T *vec);

#ifdef VEC_IMPL

void VEC_SUFFIX(expand_to)(VEC_TYPE_T *vec, size_t count)
{
	if (count > vec->cap) {
		if (vec->cap || count > VEC_INIT_SIZE) {
			// https://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2
			count--;
			count |= count >> 1;
			count |= count >> 2;
			count |= count >> 4;
			count |= count >> 8;
			count |= count >> 16;
#if __x86_64__ || __ppc64__ || _WIN64
			count |= count >> 32;
#endif
			count++;

			vec->cap = count;
		} else {
			vec->cap = VEC_INIT_SIZE;
		}
		vec->data = (VEC_TYPE*)realloc(vec->data, sizeof(VEC_TYPE) * vec->cap);
		if (!vec->data) {
			VEC_OOM_HANDLER;
		}
	}
}

void VEC_SUFFIX(shrink)(VEC_TYPE_T *vec)
{
	if (vec->count < vec->cap) {
		vec->cap = vec->count;
		vec->data = (VEC_TYPE*)realloc(vec->data, sizeof(VEC_TYPE) * vec->cap);
		if (!vec->data) {
			VEC_OOM_HANDLER;
		}
	}
}

void VEC_SUFFIX(expand_by)(VEC_TYPE_T *vec, size_t count)
{
	VEC_SUFFIX(expand_to)(vec, vec->count + count);
}

void VEC_SUFFIX(push)(VEC_TYPE_T *vec, VEC_TYPE elem)
{
	VEC_SUFFIX(expand_by)(vec, 1);
	vec->data[vec->count++] = elem;
}

void VEC_SUFFIX(push_arr)(VEC_TYPE_T *vec, const VEC_TYPE *arr, size_t count)
{
	assert(count);
	VEC_SUFFIX(expand_by)(vec, count);
	memcpy(vec->data + vec->count, arr, sizeof(VEC_TYPE) * count);
	vec->count += count;
}

VEC_TYPE VEC_SUFFIX(pop)(VEC_TYPE_T *vec)
{
	assert(vec->count);
	return vec->data[--vec->count];
}

void VEC_SUFFIX(clear)(VEC_TYPE_T *vec)
{
	vec->count = 0;
}

VEC_TYPE VEC_SUFFIX(get)(VEC_TYPE_T *vec, size_t i)
{
	assert(i < vec->count);
	return vec->data[i];
}

size_t VEC_SUFFIX(count)(VEC_TYPE_T *vec)
{
	return vec->count;
}

void VEC_SUFFIX(free)(VEC_TYPE_T *vec)
{
	free(vec->data);
	vec->data = 0;
	vec->count = vec->cap = 0;
}

#endif // VEC_IMPL

#undef VEC_TYPE
#undef VEC_NAME

#undef VEC_CONCAT
#undef VEC_CONCAT_INNER
#undef VEC_SUFFIX

#undef VEC_TYPE_T
