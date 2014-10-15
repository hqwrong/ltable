#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

#include "ltable.h"

#define SHORTSTR_LEN 128

struct pool_node {
    size_t nodesz;
    struct pool_node *next;
};

struct pool {
    struct pool_node *node;
    struct pool_node *freenode;
};

#define MAXBITS      30
#define MAXASIZE	(1 << MAXBITS)

/* use at most ~(2^LUAI_HASHLIMIT) bytes from a string to compute its hash*/
#define STR_HASHLIMIT		5

union ltable_Hash {
    double f;
    const void *p;
    long int i;
    uint8_t l_p[4];
};

struct ltable_value {
    bool setted;
    char v[1];
    /* follow vmemsz space*/
};

struct ltable_node {
    struct ltable_node *next;
    struct ltable_key key;
    struct ltable_value value;
    /* follow vmemsz space*/
};

struct ltable {
    size_t vmemsz;
    struct ltable_value *array;
    struct ltable_node *node;
    int sizearray;
    uint8_t lsizenode;          /* log2 of size of `node' array */
    struct pool pool;
    unsigned int seed;
    struct ltable_node *lastfree;
};


/*
** `module' operation for hashing (size is always a power of 2)
*/
#define lmod(s,size)  ((int)((s) & ((size)-1)))

#define gnode(t, i) (&t->node[i])
#define gnext(n)    ((n)->next)
#define sizenode(t)	(1 << ((t)->lsizenode))
#define hashnode(t, n) (gnode(t, lmod((n), sizenode(t))))
#define isnil(v) (!(v).setted)
#define inarray(t, idx) ((idx)>=0 && (idx) < (t)->sizearray)


/*
** {=============================================================
** Pool
** ==============================================================
*/

static void
pool_init(struct pool *p) {
    p->node = NULL;
    p->freenode = NULL;
}

static void*
pool_alloc(struct pool *p, size_t sz) {
    struct pool_node **np = &p->freenode;
    struct pool_node *n = NULL;
    while (*np) {
        if ((*np)->nodesz >= sz) {
            n = *np;
            *np = (*np)->next;
            break;
        }
        np = &(*np)->next;
    }
    if (!n) {
        if (sz < SHORTSTR_LEN) sz = SHORTSTR_LEN;
        n = (struct pool_node*)malloc(sz + sizeof(struct pool_node));
        n->nodesz = sz;
    }
    n->next = p->node;
    p->node = n;
    return n+1;
}

static void
pool_free(struct pool *p, struct pool_node *node) {
    struct pool_node **np = &p->node;
    node--;

    while(*np) {
        if (*np == node) {
            *np = (*np)->next;
            break;
        }
        np = &(*np)->next;
    }

    node->next = p->freenode;
    p->freenode = node;
}

static void
pool_release(struct pool *p) {
    struct pool_node *nextn;
    struct pool_node *n = p->node;
    while(n) {
        nextn = n->next;
        free(n);
        n = nextn;
    }
    n = p->freenode;
    while(n) {
        nextn = n->next;
        free(n);
        n = nextn;
    }
}


/*
** }=============================================================
*/


static void
_rehash(struct ltable* t, const struct ltable_key *ek);

static struct ltable_value *
_set(struct ltable* t, const struct ltable_key *key);

int
_ceillog2 (unsigned int x) {
  static const unsigned char log_2[256] = {
    0,1,2,2,3,3,3,3,4,4,4,4,4,4,4,4,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
    6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8
  };
  int l = 0;
  x--;
  while (x >= 256) { l += 8; x >>= 8; }
  return l + log_2[x];
}

void
_cpykey(struct ltable *t, struct ltable_key *dest, const struct ltable_key *src) {
    *dest = *src;
    if (dest->type == LTABLE_KEYSTR) {
        size_t l = strlen(src->v.s)+1;
        char *sp = pool_alloc(&t->pool, l);
        memcpy(sp, src->v.s, l);
        dest->v.s = sp;
    }
}

bool
_eqkey(const struct ltable_key *key, const struct ltable_key *nkey) {
    if (key->type != nkey->type)
        return false;

    switch (key->type) {
    case LTABLE_KEYSTR:
        return !strcmp(key->v.s, nkey->v.s);
    case LTABLE_KEYINT:
        return key->v.i == nkey->v.i;
    case LTABLE_KEYNUM:
        return key->v.f == nkey->v.f;
    default:                    /* keyobj */
        return key->v.p == nkey->v.p;
    }
}

unsigned int
_strhash (const char *str, unsigned int seed) {
    size_t l = strlen(str);
    unsigned int h = seed ^ ((unsigned int)l);
    size_t l1;
    size_t step = (l >> STR_HASHLIMIT) + 1;
    for (l1 = l; l1 >= step; l1 -= step)
        h = h ^ ((h<<5) + (h>>2) + ((uint8_t)(str[l1 - 1])));
    return h;
}

unsigned int
_numhash (union ltable_Hash *u) {
    int i = u->l_p[0] + u->l_p[1] + u->l_p[2] + u->l_p[3];

    if (i < 0) {
        if (((unsigned int)i) == 0u - i)  /* use unsigned to avoid overflows */
            i = 0;  /* handle INT_MIN */
        i = -i;  /* must be a positive value */
    }
    return i;
}

/*
** returns the index for `key' if `key' is an appropriate key to live in
** the array part of the table, -1 otherwise.
*/
static int
arrayindex (const struct ltable_key *key) {
  if (key->type == LTABLE_KEYINT) {
      return key->v.i;
  }
  return -1;  /* `key' did not match some condition */
}

static struct ltable_node *
mainposition(struct ltable* t, const struct ltable_key* key) {
    unsigned int h;
    if (key->type == LTABLE_KEYSTR)
        h = _strhash(key->v.s, t->seed);
    else {
        union ltable_Hash u;
        memset(&u, 0, sizeof(u));

        switch(key->type) {
        case LTABLE_KEYNUM:
            u.f = key->v.f; break;
        case LTABLE_KEYINT:
            u.i = key->v.i; break;
        default:                /* LTABLE_KEYOBJ  */
            u.p = key->v.p; break;
        }

        h = _numhash(&u);
    }
    return hashnode(t, h);
}

static struct ltable_node*
_getfreepos(struct ltable* t) {
    while (t->lastfree > t->node) {
        t->lastfree--;
        if (isnil(t->lastfree->value))
            return t->lastfree;
    }
    return NULL;  /* could not find a free place */
}

static struct ltable_node *
_hashget(struct ltable* t, const struct ltable_key * key) {
    struct ltable_node *node = mainposition(t, key);
    while (node) {
        if (!isnil(node->value) && _eqkey(&node->key, key))
            break;
        else
            node = gnext(node);
    }
    return node;
}

static struct ltable_value *
_get(struct ltable* t, const struct ltable_key * key) {
    int idx = arrayindex(key);
    if (inarray(t, idx)) {  /* in array part? */
        struct ltable_value *val = &t->array[idx];
        return isnil(*val) ? NULL : val;
    }

    struct ltable_node *node = _hashget(t, key);
    return node ? &node->value : NULL;    
}

static struct ltable_value *
_hashset(struct ltable* t, const struct ltable_key *key) {
    struct ltable_node *mp = mainposition(t, key);
    if (!isnil(mp->value)){      /* main position is taken? */
        struct ltable_node *othern;
        struct ltable_node *freen = _getfreepos(t);
        if (!freen) {
            _rehash(t, key);
            return _set(t, key);
        }
        othern = mainposition(t, &mp->key);
        if (othern != mp) { /* is colliding node out of its main position? */
            /* yes; move colliding node into free position */
            while (gnext(othern) != mp)
                othern = gnext(othern); /* find previous */
            gnext(othern) = freen;
            *freen = *mp; /* copy colliding node into free pos. (mp->next also goes) */
            gnext(mp) = NULL;
        }
        else { /* colliding node is in its own main position */
            /* new node will go into free position */
            gnext(freen) = gnext(mp);
            gnext(mp) = freen;
            mp = freen;
        }
    }
    _cpykey(t, &mp->key, key);
    mp->value.setted = true;
    return &mp->value;
}

static struct ltable_value *
_set(struct ltable* t, const struct ltable_key *key) {
    int idx = arrayindex(key);
    if (inarray(t, idx)) {  /* in array part? */
        t->array[idx].setted = true;
        return &t->array[idx];
    } else {
        return _hashset(t, key);
    }
}

/*
** {=============================================================
** Rehash
** ==============================================================
*/
static int
computesizes (int nums[], int *narray) {
    int i;
    int twotoi;  /* 2^i */
    int a = 0;  /* number of elements smaller than 2^i */
    int na = 0;  /* number of elements to go to array part */
    int n = 0;  /* optimal size for array part */
    for (i = 0, twotoi = 1;i<=MAXBITS; i++, twotoi *= 2) {
        if (nums[i] > 0) {
            a += nums[i];
            if (a > twotoi/2) {  /* more than half elements present? */
                n = twotoi;  /* optimal size (till now) */
                na = a;  /* all elements smaller than n will go to array part */
            }
        }
        if (*narray == a) break;
    }
    *narray = n;
    assert(*narray/2 <= na && na <= *narray);
    return na;
}


static int
countint (const struct ltable_key *key, int *nums) {
    int k = arrayindex(key);
    if (0 <= k && k <= MAXASIZE) {  /* is `key' an appropriate array index? */
        nums[k==0 ? 0 : _ceillog2(k)+1]++;  /* count as such */
        return 1;
    }
    else
        return 0;
}

static int
numusearray (const struct ltable *t, int *nums) {
    int lg;
    int ttlg;  /* 2^lg */
    int ause = 0;  /* summation of `nums' */
    int i = 0;  /* count to traverse all array keys. 0-based */
    for (lg=0, ttlg=1; lg<=MAXBITS; lg++, ttlg*=2) {  /* for each slice */
        int lc = 0;  /* counter */
        int lim = ttlg;
        if (lim > t->sizearray) {
            lim = t->sizearray;  /* adjust upper limit */
            if (i >= lim)
                break;  /* no more elements to count */
        }
        /* count elements in range [2^(lg-1), 2^lg) */
        for (; i < lim; i++) {
            if (!isnil(t->array[i]))
                lc++;
        }
        nums[lg] += lc;
        ause += lc;
    }
    return ause;
}

static int
numusehash (const struct ltable *t, int *nums, int *pnasize) {
  int totaluse = 0;  /* total number of elements */
  int ause = 0;  /* summation of `nums' */
  int i = sizenode(t);
  while (i--) {
    struct ltable_node *n = &t->node[i];
    if (!isnil(n->value)) {
      ause += countint(&n->key, nums);
      totaluse++;
    }
  }
  *pnasize += ause;
  return totaluse;
}


void
_resize_node(struct ltable *t, int size) {
    int lsize = size > 0 ? _ceillog2(size) : 1; /* at least one node */
    if (lsize > MAXBITS)
        assert(0);
    size = 1 << lsize;
    int nodememsz = sizeof(struct ltable_node) + t->vmemsz - 1;
    t->node = calloc(nodememsz, size);
    t->lsizenode = (uint8_t)lsize;
    t->lastfree = gnode(t, size); /* all positions are free */
}

void
_resize(struct ltable *t, int nasize, int nhsize) {
    int i;
    int oldasize = t->sizearray;
    int oldhsize = t->lsizenode;

    struct ltable_node *nold = t->node;  /* save old hash ... */

    /* resize hash part */
    _resize_node(t, nhsize);
    /* resize array part */
    if (nasize < oldasize) {  /* array part must shrink? */
        /* re-insert elements from vanishing slice */
        for (i=nasize; i<oldasize; i++) { /* insert extra array part to hash */
            if (!isnil(t->array[i])) {
                struct ltable_key nkey;
                ltable_intkey(&nkey, i);
                struct ltable_value *val = _hashset(t, &nkey);
                *val = t->array[i];
            }
        }
    }
    t->sizearray = nasize;
    int memsz = sizeof(struct ltable_value) + t->vmemsz -1;
    t->array = realloc(t->array, memsz * nasize);
    for (i=oldasize;i<nasize;i++) /* set growed part to zero */
        memset(t->array + i, 0, memsz);

    /* re-insert elements from hash part */
    if (nold != NULL) {         /* not in init? */
        for (i = (1<<oldhsize) - 1; i >= 0; i--) {
            struct ltable_node *old = nold+i;
            if (!isnil(old->value)) {
                struct ltable_value *val = _set(t, &old->key);
                *val = old->value;
            }
        }
        /* free old hash part */
        free(nold);
    }
}


static void
_rehash(struct ltable* t, const struct ltable_key *ek) {
    int nasize, na;
    int nums[MAXBITS+1];  /* nums[i] = number of keys with 2^(i-1) <= k < 2^i */
    int i;
    int totaluse;
    for (i=0; i<=MAXBITS; i++) nums[i] = 0;  /* reset counts */
    nasize = numusearray(t, nums);  /* count keys in array part */
    totaluse = nasize;  /* all those keys are integer keys */
    totaluse += numusehash(t, nums, &nasize);  /* count keys in hash part */
    /* count extra key */
    nasize += countint(ek, nums);
    totaluse++;
    /* compute new size for array part */
    na = computesizes(nums, &nasize);
    /* resize the table to new computed sizes */
    _resize(t, nasize, totaluse - na);
}

/*
** }=============================================================
*/


struct ltable*
ltable_create(size_t vmemsz, unsigned int seed) {
    struct ltable* t = malloc(sizeof(struct ltable));

    t->vmemsz = vmemsz;
    t->array = NULL;
    t->node = NULL;
    t->lastfree = NULL;
    t->sizearray = 0;
    t->lsizenode = 0;          /* log2 of size of `node' array */
    t->seed = seed == 0 ? LTABLE_SEED : seed;
    pool_init(&t->pool);

    _resize(t, 0, 1);
    return t;
}

void
ltable_release(struct ltable *t) {
    free(t->node);
    free(t->array);
    pool_release(&t->pool);
}

void
ltable_resize(struct ltable *t, int nasize, int nhsize) {
    _resize(t, nasize, nhsize);
}

void*
ltable_get(struct ltable *t, const struct ltable_key* key) {
    struct ltable_value *val = _get(t, key);
    return val->v;
}

void*
ltable_set(struct ltable* t, const struct ltable_key* key) {
    struct ltable_value *val = _get(t, key);
    if (!val) val = _set(t, key);
    return val->v;
}

void
ltable_del(struct ltable* t, const struct ltable_key* key) {
    if (key->type == LTABLE_KEYSTR) {
        struct ltable_node *node = _hashget(t, key);
        if (node) {
            node->value.setted = false;
            /* free string key */
            pool_free(&t->pool, (struct pool_node*)node->key.v.s);
            node->key.v.s = NULL;
        }
    } else {
        struct ltable_value *val = _get(t, key);
        if (val) 
            val->setted = false;
    }
}

void *
ltable_next(struct ltable *t, unsigned int *ip, struct ltable_key *key) {
    int nsz = sizenode(t);
    struct ltable_value * val = NULL;

    for (;*ip < t->sizearray; (*ip)++) { /* search array part */
        if (!isnil(t->array[*ip])) {
            val = &t->array[*ip];
            if (key) {
                key->type = LTABLE_KEYINT;
                key->v.i = *ip;
            }
            break;
        }
    }
    if (*ip >= t->sizearray)
        for (;*ip < nsz + t->sizearray; (*ip)++) { /* search hash part */
            struct ltable_node * node = &t->node[*ip - t->sizearray];
            if (!isnil(node->value)) {
                if (key) *key = node->key;
                val = &node->value;
                break;
            }
        }

    (*ip)++;
    return val ? val->v : NULL;
}

inline struct ltable_key*
ltable_numkey(struct ltable_key *key, double k) {
    key->type = LTABLE_KEYNUM;
    key->v.f  = k;

    return key;
}

inline struct ltable_key*
ltable_strkey(struct ltable_key *key, const char* k) {
    key->type = LTABLE_KEYSTR;
    key->v.s  = k;

    return key;
}

inline struct ltable_key*
ltable_intkey(struct ltable_key *key, long int k) {
    key->type = LTABLE_KEYINT;
    key->v.i  = k;
    return key;
}

inline struct ltable_key*
ltable_objkey(struct ltable_key *key, const void *p) {
    key->type = LTABLE_KEYOBJ;
    key->v.p  = p;
    return key;
}

/* end of ltable.c */
