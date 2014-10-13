#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#define SHORTSTR_LEN 128

struct pool_node {
    size_t nodesz;
    struct pool_node *next;
    struct pool_node *prev;
};

struct pool {
    struct pool_node *node;
    struct pool_node *freenode;
};

#define MAXBITS      30
#define MAXASIZE	(1 << MAXBITS)

/* use at most ~(2^LUAI_HASHLIMIT) bytes from a string to compute its hash*/
#define STR_HASHLIMIT		5

#define LTABLE_NUM      1
#define LTABLE_FLOAT    2
#define LTABLE_STRING   3

struct ltable_value {
    bool setted;
    char v[1];
    /* follow vmemsz space*/
};

struct ltable_key {
    int type;
    union {
        double f;
        char*  p;
    } v;
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


static void
pool_init(struct pool *p) {
    p->node = NULL;
    p->freenode = NULL;
}

static void*
pool_alloc(struct pool *p, size_t sz) {
    struct pool_node * node = p->freenode;
    while (node) {
        if (node->nodesz >= sz)
            return node+1;
        node = node->next;
    }
    if (sz < SHORTSTR_LEN) sz = SHORTSTR_LEN;
    node = (struct pool_node*)malloc(sz + sizeof(struct pool_node));
    node->nodesz = sz;
    node->next = p->freenode;
    if (p->freenode) p->freenode->prev = node;
    p->freenode = node;
    return node+1;
}

static void
pool_free(struct pool *p, struct pool_node *node) {
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->next = p->freenode;
    p->freenode = node;
}

static void
pool_release(struct pool *p) {
    struct pool_node *n = p->node;
    while(n) free(n);
    n = p->freenode;
    while(n) free(n);
}

static void
_rehash(struct ltable* t, const struct ltable_key *ek);

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

bool
_eqkey(const struct ltable_key *key, const struct ltable_key *nkey) {
    if (key->type != nkey->type)
        return false;

    switch (key->type) {
    case LTABLE_STRING:
        return !strcmp(key->v.p, nkey->v.p);
    default:
        return key->v.f == nkey->v.f;
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

union num_Cast { double l_d; uint32_t l_p[2];};
unsigned int
_numhash (double n) {
    union num_Cast u = {.l_d = n + 1.0};/* avoid -0 */
    int i = u.l_p[0] + u.l_p[1];

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
  if (key->type == LTABLE_NUM) {
    double n = (double)key->v.f;
    int k = (int)n;
    if (k == n)
      return k - 1;             /* !! CANNOT FIGURE OUT WHY */
  }
  return -1;  /* `key' did not match some condition */
}

static struct ltable_node *
mainposition(struct ltable* t, const struct ltable_key* key) {
    switch (key->type) {
    case LTABLE_NUM:
        return hashnode(t, _numhash(key->v.f));
    default:
        return hashnode(t, _strhash(key->v.p, t->seed));
    }
}

static struct ltable_node*
_getfreepos(struct ltable* t) {
    while (t->lastfree > t->node) {
        t->lastfree--;
        if (!t->lastfree->value.setted)
            return t->lastfree;
    }
    return NULL;  /* could not find a free place */
}

static struct ltable_value *
_get(struct ltable* t, const struct ltable_key * key) {
    int idx = arrayindex(key);
    if (inarray(t, idx)) {  /* in array part? */
        struct ltable_value *val = &t->array[idx];
        return isnil(*val) ? NULL : val;
    }

    struct ltable_node *node = mainposition(t, key);
    while (node) {
        if (!isnil(node->value) && _eqkey(&node->key, key))
            break;
        else
            node = gnext(node);
    }
    return node ? &node->value : NULL;
}

static struct ltable_value *
_set(struct ltable* t, struct ltable_key *key) {
    struct ltable_value *val;
    int idx = arrayindex(key);
    if (inarray(t, idx)) {  /* in array part? */
        val = &t->array[idx];
    } else {
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
        mp->key = *key;
        val = &mp->value;
    }

    val->setted = true;
    return val;
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
    for (i = 0, twotoi = 1; twotoi/2 < *narray; i++, twotoi *= 2) {
        if (nums[i] > 0) {
            a += nums[i];
            if (a > twotoi/2) {  /* more than half elements present? */
                n = twotoi;  /* optimal size (till now) */
                na = a;  /* all elements smaller than n will go to array part */
            }
        }
        if (a == *narray) break;  /* all elements already counted */
    }
    *narray = n;
    assert(*narray/2 <= na && na <= *narray);
    return na;
}


static int
countint (const struct ltable_key *key, int *nums) {
  int k = arrayindex(key);
  if (0 < k && k <= MAXASIZE) {  /* is `key' an appropriate array index? */
    nums[_ceillog2(k)]++;  /* count as such */
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
    int i = 1;  /* count to traverse all array keys */
    for (lg=0, ttlg=1; lg<=MAXBITS; lg++, ttlg*=2) {  /* for each slice */
        int lc = 0;  /* counter */
        int lim = ttlg;
        if (lim > t->sizearray) {
            lim = t->sizearray;  /* adjust upper limit */
            if (i > lim)
                break;  /* no more elements to count */
        }
        /* count elements in range (2^(lg-1), 2^lg] */
        for (; i <= lim; i++) {
            if (!isnil(t->array[i-1]))
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
                struct ltable_key nkey = {.type=LTABLE_NUM, .v = {.f = i}};
                struct ltable_value *val = _set(t, &nkey);
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
    int nums[MAXBITS+1];  /* nums[i] = number of keys with 2^(i-1) < k <= 2^i */
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



void*
ltable_numget(struct ltable *t, double key) {
    struct ltable_key nkey = {.type=LTABLE_NUM, .v = {.f = key}};
    struct ltable_value *val = _get(t, &nkey);
    return val->v;
}

void*
ltable_numset(struct ltable* t, double key) {
    struct ltable_key nkey = {.type=LTABLE_NUM, .v = {.f = key}};
    struct ltable_value *val = _get(t, &nkey);
    if (!val) val = _set(t, &nkey);
    return val->v;
}

void*
ltable_strget(struct ltable *t, const char* key) {
    struct ltable_key nkey = {.type=LTABLE_STRING, .v = {.p = key}};
    struct ltable_value * val = _get(t, &nkey);
    return val->v;
}

void*
ltable_strset(struct ltable* t, const char* key) {
    struct ltable_key nkey = {.type=LTABLE_STRING, .v = {.p = key}};
    struct ltable_value *val = _get(t, &nkey);
    if (!val) {
        size_t keylen = strlen(key)+1;
        nkey.v.p = pool_alloc(&t->pool, keylen);
        memcpy(nkey.v.p, key, keylen);
        val = _set(t, &nkey);
    }
    return val->v;
}

struct ltable*
ltable_create(size_t vmemsz, unsigned int seed) {
    struct ltable* t = malloc(sizeof(struct ltable));

    t->vmemsz = vmemsz;
    t->array = NULL;
    t->node = NULL;
    t->lastfree = NULL;
    t->sizearray = 0;
    t->lsizenode = 0;          /* log2 of size of `node' array */
    t->seed = seed == 0 ? 8348129 : seed;
    pool_init(&t->pool);

    _resize(t, 0, 1);
    return t;
}

void
ltable_resize(struct ltable *t, int nasize, int nhsize) {
    _resize(t, nasize, nhsize);
}

/* end of ltable.c */
