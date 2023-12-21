#pragma once

#ifndef NULL
#define NULL 0
#endif

typedef unsigned long size_t;
typedef unsigned long ptrdiff_t;
typedef unsigned long uintptr_t;
typedef unsigned long intmax_t;
typedef long ssize_t;

void free(void *mem);
void *malloc(size_t size);
void *realloc(void *mem, size_t newSize);

#define abs(v) __builtin_abs(v)

typedef __builtin_va_list va_list;
#define va_start(v,l)  __builtin_va_start(v,l)
#define va_arg(v,l)	   __builtin_va_arg(v,l)
#define va_end(v)      __builtin_va_end(v)
#define va_copy(d,s)   __builtin_va_copy(d,s)
