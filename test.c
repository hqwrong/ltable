#include <stdio.h>
#include "ltable.h"

static void
_dump(struct ltable *t) {
    unsigned int i = 0;
    int *p;
    struct ltable_key *kp = NULL;
    while (p = ltable_next(t, &i, &kp)) {
        switch (kp->type) {
        case LTABLE_KEYNUM:
            printf("key=%.3f, val=%d\n", kp->v.f, *p);
            break;
        case LTABLE_KEYINT:
            printf("key=%d, val=%d\n", kp->v.i, *p);
            break;
        case LTABLE_KEYSTR:
            printf("key=[%s], val=%d\n", kp->v.s, *p);
        default:                /* LTABLE_KEYOBJ */
            printf("key=[%p], val=%d\n", kp->v.p, *p);
        }
    }
    printf("===========\n");
}

int
main() {
    struct ltable_key key;
    struct ltable* t = ltable_create(sizeof(int), 0);
    int *p;

    /* /\* t["foo"] = 12 *\/ */
    /* p = ltable_set(t, ltable_strkey(&key, "foo"));  */
    /* *p = 12; */

    /* /\* t[3.5] = 13 *\/ */
    /* p = ltable_set(t, ltable_numkey(&key, 3.5)); */
    /* *p = 13; */

    /* t[1] = 14 */
    p = ltable_set(t, ltable_intkey(&key, 1));
    *p = 14;

    /* t[&obj] = 15 */
    int obj = 0;
    p = ltable_set(t, ltable_objkey(&key, &obj));
    *p = 15;
    
    _dump(t);

    struct ltable* t2 = ltable_create(sizeof(struct ltable*), 0);
    struct ltable** tp = ltable_set(t2, ltable_strkey(&key, "table"));
    *tp = t;
    printf("t2['table'] = %p\n", *((struct ltable**)ltable_get(t2, &key)));
}
