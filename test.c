#include <stdio.h>
#include "ltable.h"

static void
_dumparray(struct ltable *t) {
    unsigned int i = 0;
    int *p;
    printf("iter array:\n");
    while (p = ltable_getn(t, i)) {
        printf("\t%d: %d\n", i, *p);
        i++;
    }
}

static void
_dump(struct ltable *t) {
    unsigned int i = 0;
    int *p;
    struct ltable_key kp;
    
    printf("iter table:\n");
    i = 0;
    while (p = ltable_next(t, &i, &kp)) {
        switch (kp.type) {
        case LTABLE_KEYNUM:
            printf("\tkey=%.3f, val=%d\n", kp.v.f, *p);
            break;
        case LTABLE_KEYINT:
            printf("\tkey=%d, val=%d\n", kp.v.i, *p);
            break;
        case LTABLE_KEYSTR:
            printf("\tkey=['%s'], val=%d\n", kp.v.s, *p);
            break;
        case LTABLE_KEYOBJ:
            printf("\tkey=[%p], val=%d\n", kp.v.p, *p);
            break;
        default :
            printf("\terror type %d\n", kp.type);
        }
    }
}

int
main() {
    struct ltable_key key;
    struct ltable* t;
    int *p;

    /*****************************/
    /* construct a simple table*/
    /*****************************/

    t = ltable_create(sizeof(int), 0);
    /* t["foo"] = 12 */
    p = ltable_set(t, ltable_strkey(&key, "foo"));
    *p = 12;

    /* t[3.5] = 13 */
    p = ltable_set(t, ltable_numkey(&key, 3.5));
    *p = 13;

    /* t[&obj] = 20 */
    int obj = 0;
    p = ltable_set(t, ltable_objkey(&key, &obj));
    *p = 20;

    int i;
    for(i=0;i<10;i++) {
        p = ltable_set(t, ltable_intkey(&key, i));
        *p = i+1;
    }

    _dumparray(t);

    for(i=9;i>=0;i--){
        ltable_del(t, ltable_intkey(&key, i));
    }

    p = ltable_set(t, ltable_strkey(&key, "bar"));
    *p = 99;
    p = ltable_set(t, ltable_strkey(&key, "hello,world"));
    *p = 100;
    p = ltable_set(t, ltable_strkey(&key, "hqwrong.github.io"));
    *p = 101;

    _dump(t);

    ltable_release(t);
}
