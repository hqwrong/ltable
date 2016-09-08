C hash lib inspired by Lua's Table
=====================================

## APIS
### Create
```
  struct ltable*  ltable_create(size_t vmemsz, unsigned int seed);
```
create a table instance.

`seed` is used to gen hash value, `LTABLE_SEED` is used when 0 supplied.


`vmemsz` is the maximum memory size a single table value can occupy. If you have multiple types of table value, I recommend you to use a tag union to group them, for example:

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
4 types of key are supported

```
LTABLE_KEYNUM      1
LTABLE_KEYINT      2
LTABLE_KEYSTR      3
LTABLE_KEYOBJ      4

```
Use corresponding function to create them, like `ltable_intkey` to gen int-type key, `ltable_numkey` for double-type key, e.t.c.

### Get, Set and Del

```
void* ltable_get(struct ltable* t, const struct ltable_key* key);
void* ltable_set(struct ltable* t, const struct ltable_key* key);
void  ltable_del(struct ltable* t, const struct ltable_key* key);
```

`ltable_get` will return the addr of value if it finds any, otherwise `NULL` returned.

`ltable_set` returns the same with `ltable_get` when the key is found, but it will create a new one otherwise.


### Iter
use `ltable_next` to iter among table.
```
void* ltable_next(struct ltable *t, unsigned int *ip, struct ltable_key *key);
```
returns the value addr, or `NULL` when no more item to be iterated.

`ip` is the iter handle, which must initialize to 0 at beginning of iteration.

`key` will be filled with corresponding key. set it to `NULL` if you don't need it.

### Array
ltable's array is 0-based, which is different from Lua's 1-based array.
For your convenience, ltable offered an auxiliary function to fetch array item, insead of `ltable_get`:
```
  void* ltable_getn(struct ltable* t, int i);
```
To iter on array is easy:
```
int i = 0;
while (p = ltable_getn(t, i++)) {...}
```

## EXAMPLES
see `test.c`


