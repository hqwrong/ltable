#include <stdio.h>
#include "ltable.h"


int
main() {
    struct ltable* t = ltable_create(sizeof(int), 0);
    int *p = ltable_strset(t, "foo");
    *p = 123;
    p = ltable_strget(t, "foo");
    printf("t.foo = %d\n", *p);

    p = ltable_numset(t, (double)1);
    *p = 56;
    p = ltable_numget(t, (double)1);
    printf("t[1] = %d\n", *p);
}
