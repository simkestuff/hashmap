# skhashmap: a single-header hash map for C

A hash map for C projects, in **one header file**. Keys are arbitrary byte
sequences, values are fixed-size blocks of bytes, and everything is copied into
the map. It uses separate chaining, grows itself, and has an iterator. It is a
learning project, so the code is short and meant to be read.

This README is also a reminder to future me: how it is built, how to use it,
and what it can't do. 
AI wrote this README.

## Contents

- [Project layout](#project-layout)
- [Using the library](#using-the-library)
- [API reference](#api-reference)
- [How it works](#how-it-works)
- [Limitations](#limitations)
- [Tests](#tests)
- [Development notes](#development-notes)
- [Possible next steps](#possible-next-steps)

## Project layout

```
.
└── src/
    ├── hashmap.h     # the whole library: declarations and implementation
    └── test.c        # tests, with its own main(); not part of the library
```

The library is the single file `hashmap.h`. It depends only on the C standard
library and never prints anything. To use it in another project, copy that one
file (or add this repository as a git submodule and point `-I` at `src/`).

Written for C99 or later (`static inline`, `bool`). Built and tested with
GCC 13 on Linux, with `-std=c11 -Wall -Wextra -pedantic`.

## Using the library

### The one rule: where the implementation goes

`hashmap.h` is both the header and the implementation. The function bodies are
only compiled where you ask for them:

- In **exactly one** `.c` file, define `HM_IMPLEMENTATION` before including the
  header. That file then contains the code of every `hm_*` function.
- In **every other** file, include the header without the define. They only get
  the declarations.

```c
#define HM_IMPLEMENTATION      // only in ONE .c file
#include "hashmap.h"
```

Two mistakes to recognize:

| Mistake | What you see |
|---|---|
| `HM_IMPLEMENTATION` defined in two files | "multiple definition" errors when linking |
| `HM_IMPLEMENTATION` defined in no file | "undefined reference to `hm_create`" errors when linking |
| the header included twice in the file that defines `HM_IMPLEMENTATION` (for example through another header) | "redefinition of `hash_djb2`" and similar errors when compiling |

The third one is why the safest habit is a **dedicated implementation file**
that contains nothing else:

```c
// hashmap_impl.c
#define HM_IMPLEMENTATION
#include "hashmap.h"
```

### Quick example: counting words

A program in one file, so it defines `HM_IMPLEMENTATION` itself:

```c
#include <stdio.h>
#define HM_IMPLEMENTATION
#include "hashmap.h"

int main(void)
{
    const char *words[] = {"to", "be", "or", "not", "to", "be"};
    size_t n = sizeof words / sizeof words[0];

    Hashmap *counts = hm_create(sizeof(int), 16, NULL);
    if (counts == NULL) return 1;

    for (size_t i = 0; i < n; i++) {
        int *c = hm_get_str(counts, words[i]);
        if (c != NULL) {
            (*c)++;                       // update in place through the pointer
        } else {
            int one = 1;
            if (hm_put_str(counts, words[i], &one) == NULL) {
                hm_destroy(counts);
                return 1;                 // allocation failed
            }
        }
    }

    HashmapIter it;
    hm_iter_init(&it, counts);
    while (hm_iter_next(&it)) {
        printf("%.*s: %d\n", (int)hm_iter_key_len(&it),
               (const char *)hm_iter_key(&it), *(int *)hm_iter_value(&it));
    }
    printf("distinct words: %zu\n", hm_count(counts));

    hm_destroy(counts);
    return 0;
}
```

Compile it with the header in the same folder (or add `-Ipath/to/src`):

```
gcc -std=c11 -Wall -Wextra -o wordcount wordcount.c
```

The map stores **bytes**. A key is a pointer and a length, and a value is
always exactly `value_size` bytes, a size you choose once, when you create the
map. Both are copied in, so the caller's variables can change or go out of
scope afterwards. The three `_str` functions are wrappers for NUL-terminated
string keys (the terminator is not part of the key). For any other key type use
`hm_put`, `hm_get`, and `hm_delete` with an explicit length.

### Other key and value types

```c
typedef struct { char name[16]; double score; } Player;
typedef struct { char tag; int id; } Key;       // has padding bytes after `tag`

// integer keys, struct values
Hashmap *players = hm_create(sizeof(Player), 8, NULL);
Player p = { .name = "alice", .score = 9.5 };
int id = 42;
hm_put(players, &id, sizeof id, &p);
Player *got = hm_get(players, &id, sizeof id);  // NULL if missing

// struct keys: zero the whole struct first, or padding bytes break lookups
Key k;
memset(&k, 0, sizeof k);
k.tag = 'a';
k.id = 7;
```

### Iterating

```c
HashmapIter it;
hm_iter_init(&it, map);
while (hm_iter_next(&it)) {
    const void *key = hm_iter_key(&it);
    size_t key_len = hm_iter_key_len(&it);
    void *value = hm_iter_value(&it);
    /* ... */
}
```

The iterator lives on the stack, so there is nothing to allocate or free. Every
entry is visited exactly once, in an **unspecified order** (not insertion
order, and it can change after the map grows).

What you may do to the map inside the loop:

| Action | Allowed? |
|---|---|
| read values, write through `hm_iter_value` | yes |
| `hm_put` on a key that already exists | yes |
| `hm_delete` of the entry just returned | yes |
| `hm_put` of a **new** key (the map may grow and rearrange itself) | no |
| `hm_delete` of any **other** entry | no |
| `hm_destroy` and then keep using the iterator | no |

After a "no" action, entries can be skipped, visited twice, or read from freed
memory. Start a new iterator instead.

### A project with several files

Only one file gets the implementation. The others just include the header, even
if one header includes it for them:

```c
// counter.h
#include "hashmap.h"
Hashmap *make_counter(void);

// counter.c            (no HM_IMPLEMENTATION here)
#include "counter.h"
Hashmap *make_counter(void) { return hm_create(sizeof(int), 8, NULL); }

// main.c               (no HM_IMPLEMENTATION here either)
#include "counter.h"

// hashmap_impl.c       (the only place with the define)
#define HM_IMPLEMENTATION
#include "hashmap.h"
```

```
gcc -std=c11 -Wall -Wextra -Ipath/to/src main.c counter.c hashmap_impl.c -o app
```

### Optional: a static or shared library

A single-header library doesn't need one, and copying `hashmap.h` is simpler.
If you still want a library file, build it from the dedicated implementation
file:

```
# static library
gcc -std=c11 -Wall -Wextra -O2 -Ipath/to/src -c hashmap_impl.c -o hashmap_impl.o
ar rcs libskhashmap.a hashmap_impl.o

# shared library
gcc -std=c11 -Wall -Wextra -O2 -fPIC -shared -Ipath/to/src hashmap_impl.c -o libskhashmap.so
```

Programs that use the library include `hashmap.h` **without** the define and
link the library:

```
# static: pass the archive file
gcc -std=c11 -Ipath/to/src main.c libskhashmap.a -o app

# shared: the loader must find the .so at run time, so bake the folder in
gcc -std=c11 -Ipath/to/src main.c -L. -lskhashmap -Wl,-rpath,/absolute/path/to/lib -o app
```

One trap: with `-lskhashmap`, the linker picks the **shared** library when both
`.a` and `.so` are in the same folder. To force the static one, pass the `.a`
path directly, or write `-Wl,-Bstatic -lskhashmap -Wl,-Bdynamic`. A program
that fails at start with "cannot open shared object file" has hit this.

## API reference

All declarations are in `hashmap.h`, with a comment on each function.

| Function | What it does |
|---|---|
| `hm_create(value_size, buckets_count, hash)` | Create an empty map. Both sizes must be greater than 0. `hash` may be `NULL` for the default. Returns `NULL` on invalid arguments or allocation failure. |
| `hm_destroy(hm)` | Free every entry, the bucket array, and the map. `hm_destroy(NULL)` does nothing. |
| `hm_put(hm, key, key_len, value)` | Store a copy of `value` (`value_size` bytes) under the key. If the key exists, only its value is replaced. Returns a pointer to the stored value, or `NULL` if allocation failed (the map is unchanged). |
| `hm_get(hm, key, key_len)` | Pointer to the stored value, or `NULL` if the key is not present. |
| `hm_delete(hm, key, key_len)` | Remove the entry. Returns `true` if it existed. |
| `hm_count(hm)` | Number of entries. 0 for a `NULL` map. |
| `hm_put_str`, `hm_get_str`, `hm_delete_str` | The same three operations for NUL-terminated string keys. |
| `hm_iter_init(it, hm)` | Start an iterator before the first entry. A `NULL` or empty map gives a walk that is finished from the start. |
| `hm_iter_next(it)` | Move to the next entry. Returns `false` when there are no more (and keeps returning `false`). |
| `hm_iter_key(it)`, `hm_iter_key_len(it)`, `hm_iter_value(it)` | The current entry's key bytes (read only, not NUL-terminated), their number, and a pointer to the value. They return `NULL` or 0 before the first `hm_iter_next` and after the end. |

Rules that go with the API:

- **Keys are equal** if they have the same length and the same bytes. `key`
  must be non-`NULL` even when `key_len` is 0 (the empty key `""` is allowed).
- **Returned pointers** point at the map's own copy of the value. Writing
  through one changes the stored value (`*p += 1` works). A pointer from
  `hm_put` or `hm_get` is valid until the next `hm_put`, `hm_delete`, or
  `hm_destroy`, so copy the value out if you need it longer.
- **Missing keys** are reported as `NULL`, so any value, including 0, is a
  legal value.
- **A custom hash function** (type `HmHashFn`) must return the same value for
  equal keys and depend on nothing but the bytes it is given.

## How it works

### Why one header

The file has two parts. The first, inside an include guard, holds what every
user needs: the struct definitions, the function declarations, and the small
`static inline` string wrappers. The second part holds all the function bodies
and sits behind `#ifdef HM_IMPLEMENTATION`, so it is compiled only in the one
file that asks for it. That is the whole trick: the same file works as a
header everywhere and as source in one place.

### Data structure

```
Hashmap
  buckets ──► [0] ──► NULL
              [1] ──► HashmapNode ──► HashmapNode ──► NULL
              [2] ──► HashmapNode ──► NULL
              [3] ──► NULL
  buckets_count   number of slots in the array above
  count           number of entries stored
  value_size      bytes per value, fixed at creation
  hash            hash function (default: djb2)

HashmapNode
  key, key_len    heap copy of the key bytes, and their number
  value           heap copy of the value bytes
  next            next node in the same bucket, NULL at the end
```

- `buckets` is a `HashmapNode **`: an array of `buckets_count` pointers. Each one is
  the head of a linked list (a *chain*) or `NULL` for an empty bucket.
- A key goes to bucket `hash(key) % buckets_count`. Different keys can land in
  the same bucket, and that is the normal case, so every node stores its own
  copy of the key. The hash only chooses where to look, and a lookup then
  compares keys.
- Each entry is three separate allocations: the node, the key bytes, and the
  value bytes. Because the value has its own `malloc`, a pointer to it is
  suitably aligned for any type.
- The struct definitions are in the header because a single header has nowhere
  else to put them, but their fields are **private by convention**: use the
  `hm_*` functions, never the fields.

### Operations

- **get:** hash the key, pick the bucket, walk the chain, and return the value
  of the first node whose key has the same length and the same bytes
  (`key_len ==` and `memcmp`). The end of the chain means "missing".
- **put:** first do a `get`. If the key exists, copy the new value over the old
  one (with `memmove`, so passing the pointer from `hm_get` back in is safe)
  and return. Otherwise, resize if needed (below), allocate the node, the key
  copy, and the value copy, and link the node at the **front** of its chain.
  If any allocation fails, whatever was already allocated is freed and the map
  is unchanged. `count` increases only for a genuinely new key.
- **delete:** walk the chain keeping a pointer to the previous node, unlink the
  match (either the bucket head or `prev->next`), free its key, value, and
  node, and decrease `count`.
- **destroy:** walk every chain and free each node's key, value, and node, then
  the bucket array and the map.

### Growth

With too few buckets, chains get long and lookups slow down. When a **new**
key is about to be added and `count * 4 >= buckets_count * 3` (the table is at
least 75% full), the bucket array is doubled first:

1. allocate a new array of twice the size;
2. move every node into it by relinking: its new bucket is computed from the
   hash again, but no key or value is copied or reallocated;
3. free the old array (only the array, not the nodes) and swap in the new one.

If the allocation of the new array fails, the map keeps working with the old
one, only slower, and the `put` continues. The initial bucket count can be any
number above 0, and the table keeps doubling from there.

### Hash function

The default is djb2 over the key bytes (`hash = hash * 33 + byte`, starting at
5381, with unsigned wrap-around). It is simple and spreads typical keys well
enough. A custom function can be passed to `hm_create`; the tests use one that
returns 0 for every key to force every entry into a single chain.

### Iterator

An `HashmapIter` is a small struct on the caller's stack with four fields: the
map, `current`, `following`, and `next_bucket`.

- `current` is the entry last returned, and `NULL` before the first
  `hm_iter_next` and after the end. The accessors read it, and return `NULL` or
  0 when it is `NULL`.
- `following` is the entry the **next** call will return. It is found one step
  ahead: the next node in the same chain, or else the head of the next
  non-empty bucket. That is why deleting `current` is safe, since the iterator
  never needs to read the freed node.
- `next_bucket` is where to continue searching when the current chain ends.
- `hm_iter_next` hands out `following` as the new `current` and works out the
  new `following`. Its return value means "this call produced an entry".
- A new key can make the map grow and relink every node into new buckets, so
  an iterator running across that would skip or repeat entries. That is why
  adding keys during iteration is not allowed.

## Limitations

**Design limits**

- **One value type and size per map.** The size is fixed at creation. For
  different value types, use a map per type, or store a pointer.
- **Pointer values are not managed.** The map copies the bytes of a value, so
  if the value is a pointer to heap memory, the map never frees what it points
  to, not on delete, not on overwrite, and not on destroy. There is no
  per-value free callback. You must free those objects yourself.
- **Iteration has rules.** The order is unspecified, adding keys or deleting
  other entries during a walk is not allowed (see [Iterating](#iterating)), and
  a walk costs `buckets_count + count`.
- **No shrinking.** The table only grows. Deleting entries does not reduce the
  bucket count until the map is destroyed, and the iterator still scans every
  bucket.
- **Not thread-safe.** Don't use one map from several threads without your own
  locking.
- **Returned pointers can go stale.** Only "valid until the next `hm_put`,
  `hm_delete`, or `hm_destroy`" is promised. In this chaining implementation a
  pointer to a value actually survives a resize, but don't depend on it, since
  another implementation (open addressing, for instance) could not keep it.
- **Resize failures are silent.** If growing the table fails, the map carries
  on slower, with no way for the caller to know.

**Single-header limits**

- **The fields are visible.** The structs are in the header, so the compiler
  can't stop your code from touching `count` or `buckets`. Doing so breaks the
  map. Treat them as private.
- **Public names can clash with yours.** The types `HashmapNode`, `Hashmap`, and
  `HashmapIter` are declared in every file that includes the header, so a
  project with its own `HashmapNode` type won't compile. The static helpers
  (`hash_djb2`, `hm_get_bucket`, `hm_resize`) can clash with names in the one
  file that defines `HM_IMPLEMENTATION`.
- **The implementation part must not be included twice** in the file that
  defines `HM_IMPLEMENTATION` (see [the one rule](#the-one-rule-where-the-implementation-goes)).
  A dedicated implementation file avoids it.
- **The implementation must be compiled as C, not C++** (it converts `void *`
  implicitly). C++ code can use the declarations if the header is wrapped in
  `extern "C"` and the implementation is compiled as C.

**Keys**

- **A key is its raw bytes.** Equal-looking keys must have equal bytes.
- **Structs need zeroing before use as keys.** Padding bytes between members
  are unspecified, so two equal-looking structs may hash differently. Use
  `memset(&k, 0, sizeof k)` first.
- **Don't use floating-point keys.** `0.0` and `-0.0` are equal but have
  different bytes, and `NaN` is not equal to itself.
- **A pointer as a key hashes the pointer, not what it points to.** For
  strings use the `_str` functions or pass the characters and their length.
- **Don't store or compare raw-byte hashes across machines.** Integer keys
  depend on the byte order of the machine that wrote them.

**Performance and memory**

- Average cost per operation is constant, but chains grow when a hash function
  puts many keys in one bucket (a bad custom hash makes every operation walk
  the whole chain).
- Three allocations per entry cost memory (each allocation has its own
  bookkeeping overhead) and cache locality, since walking a chain jumps between
  unrelated places in memory. Small keys and values pay the most.
- The default hash has no protection against deliberately crafted keys that
  collide, so don't use it for hostile input.
- Speed has **not been benchmarked** yet, so there are no numbers here.

## Tests

`src/test.c` has its own `main()` and defines `HM_IMPLEMENTATION` itself, so it
needs nothing besides the header. Build it with the sanitizers and run it:

```
mkdir -p build
gcc -std=c11 -Wall -Wextra -ggdb -fsanitize=address,undefined -o build/test src/test.c
./build/test
```

AddressSanitizer finds out-of-bounds access and use-after-free, and also
reports leaks when the program exits, so keep it on. For timing, build a
separate binary with `-O2` and no sanitizers.

Each test prints its name and `ok`. A failing assertion prints the file and
line, and the last test name shown is the one that failed. `test.c` refuses to
compile with `-DNDEBUG`, because that removes every `assert` and the tests would
pass without checking anything.

What it covers:

- creation with invalid arguments, the empty map, and `NULL` handling
- put, get, overwrite (count unchanged), delete, and re-insert after delete
- the value copied in, writing through the returned pointer, and overwriting a
  value with the pointer from `hm_get`
- the key copied in (the caller's buffer changes afterwards), keys that share
  a prefix (`"ab"` and `"abc"`), the empty key, integer keys, and a zeroed
  struct key
- a struct value and a 256-byte value
- deleting the head, a middle node, and the last node of a chain (using a
  hash that puts everything in one bucket)
- a **stress test**: thousands of inserts, lookups, deletes, and re-inserts,
  ending with `hm_destroy` while entries are still inside
- a **randomized test** that compares the map against a trivially correct
  model after every operation, over a small set of keys so overwrites and
  re-inserts keep happening, with fixed seeds so a failure repeats exactly

The stress and random tests run with bucket counts 1, 2, 7, and 1024, each
with the default hash and with the constant hash. Starting from tiny tables
makes the map resize many times in the middle of a run.

Not covered: `malloc` failing (so the failure paths in `hm_put` can only be
checked by reading the code), and the iterator, which still needs its tests in
`test.c` (see the next steps).

## Development notes

Mistakes that cost time, kept as a reminder:

- **Resize forgot to reset `next`.** When the target bucket was empty, a moved
  node kept pointing at its old chain, so nodes ended up in several chains and
  `hm_destroy` freed them twice (AddressSanitizer reported the double free).
  The fix is `node->next = new_buckets[b]` unconditionally.
- **Leaks on allocation-failure paths.** With three allocations per entry, each
  failure has to free exactly what already succeeded. Tests don't make `malloc`
  fail, so these paths were found by reading.
- **`malloc(0)` may return `NULL`.** An empty key (length 0) must still
  allocate at least one byte, or it looks like an allocation failure.
- **Missing length check** would make `"ab"` and `"abc"` equal. Compare
  lengths first, then bytes.
- **Padding bytes in struct keys** make equal-looking keys different. Zero the
  struct before filling it.
- **A "1-bucket" map doesn't stay at 1 bucket**, because it grows on the
  second insert. Chain tests use a constant hash function instead.
- **A leak in the test program itself:** `return 0;` before `hm_destroy(map);`
  made the destroy unreachable, and LeakSanitizer reported the whole map. GCC
  gave no warning.
- **First iterator version skipped entries.** `hm_iter_next` returned `false`
  when it moved onto the last entry, because the result answered "is there
  something after this?" instead of "did this call produce an entry?". With a
  `while (hm_iter_next(&it))` loop, the first and the last entry were never
  visited.
- **`iter = NULL;` inside an init function does nothing for the caller.** It
  only changes the local parameter, so the caller's struct stayed
  uninitialized. Initialize the fields into the finished state instead.
- **Iterator accessors dereferenced a `NULL` current entry.** Calling them
  before the first `hm_iter_next` or after the end crashed. They now return
  `NULL` or 0.
- **Single-header trap:** the implementation part is outside the include guard,
  so including the header twice in the file that defines `HM_IMPLEMENTATION`
  gives redefinition errors. Keep a dedicated implementation file, or guard
  the implementation with a second macro.

## Possible next steps

- iterator tests in `test.c`: every entry exactly once (several sizes, bucket
  counts, and both hashes), a `NULL` and an empty map, calling `hm_iter_next`
  again after the end, deleting the current entry in the loop, and the
  accessors before the first `hm_iter_next` and after the end
- Prefix the static helpers, so they can't clash with names in the user's code
- guard the implementation part against being included twice
- a benchmark (inserts, hits, misses, deletes, different load factors, good and
  bad hash), with the results saved in this README
- an optional free callback for values that own memory
- an open-addressing version behind the same API, compared with this one on
  the benchmark
- typed convenience wrappers (macros or `static inline` functions) for the key
  and value types used most
