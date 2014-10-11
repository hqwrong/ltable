#ifndef LTABLE_H
#define LTABLE_H

#include <stdbool.h>

struct ltable;

void  ltable_init(struct ltable *t, size_t nodememsz, unsigned int seed);
void  ltable_release(struct ltable *);

void* ltable_strget(struct ltable* t, const char* key);
void* ltable_strset(struct ltable* t, const char* key);

void* ltable_numget(struct ltable *t, double key);
void* ltable_numset(struct ltable *t, double key);

#endif
