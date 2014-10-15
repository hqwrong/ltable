A C hash lib inspired by Lua's Table
=====================================

## APIS
### Create
```
  struct ltable*  ltable_create(size_t vmemsz, unsigned int seed);
```
create a table instance.

`seed` which you supply to `ltable` to gen hash value. `ltable` will use `LTABLE_SEED` when 0 supplied.


`vmemsz` is the maximum memory size your table value can occupy. If you have multiple types of table value, I recommend you to use a tag union to group them, for example:

```
struct TLValue {
    int type;
    union {
        double f;
        char * s[128];
        void *p;
    } u;
};

struct ltable* t = ltable_create(sizeof(struct TLValue), 0);
```

### Key
4 types of keys are supported

```
LTABLE_KEYNUM      1
LTABLE_KEYINT      2
LTABLE_KEYSTR      3
LTABLE_KEYOBJ      4

```
Use corresponding function to create them, like `ltable_intkey` to gen int-type key.

### Get, Set and Del

```
void* ltable_get(struct ltable* t, const struct ltable_key* key);
void* ltable_set(struct ltable* t, const struct ltable_key* key);
void  ltable_del(struct ltable* t, const struct ltable_key* key);
```

`ltable_get` will return the addr of value if it finds the item, if not `NULL` returned.

`ltable_set` returns the same with `ltable_get` when a item is found, but it will create a new one otherwise.


### Iter
use `ltable_next` to iter among table.
```
void* ltable_next(struct ltable *t, unsigned int *ip, struct ltable_key *key);
```
returns the value addr, or `NULL` when no more item to iterate.

`ip` is the iter handle, which must initialize to 0 at beginning of iteration.

`key` is used to fetch to corresponding key. set it to `NULL` if you don't need it.

