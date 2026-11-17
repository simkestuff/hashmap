# skhashmap: a small hash map library in C

A hash map for C projects: keys are arbitrary byte sequences, values are
fixed-size blocks of bytes, and everything is copied into the map. It uses
separate chaining, grows itself, and is built as a learning project, so the
code is short and meant to be read.

This README is also a reminder to future me: how it is built, how to use it,
and what it can't do.

## Contents

- [Project layout](#project-layout)
- [Building](#building)
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
├── Makefile
└── src/
    ├── hashmap.h     # public API (the only header a user needs)
    ├── hashmap.c     # the whole implementation
    └── test.c        # tests, with its own main(); not part of the library
```

`make` creates a `build/` folder (debug objects and the test binary) and
`build/release/` (the library files). Both can be deleted with `make clean`.

The library is just `hashmap.h` and `hashmap.c`. They depend only on the C
standard library and don't print anything.

## Building

Needs `gcc` and `make`. The debug build also needs the sanitizer runtime
(`libasan` and `libubsan`, which come with a normal GCC install on most
Linux distributions). Built and tested with GCC 13 on Linux.

| Command | Result |
|---|---|
| `make` | debug build of the library and the tests: `build/test` |
| `make static` | static library `build/release/libskhashmap.a` |
| `make shared` | shared library `build/release/libskhashmap.so` |
| `make obj` | only compile the debug objects |
| `make clean` | delete the `build/` folder |

Two sets of flags are used:

- **Debug** (`make`): `-Wall -Wextra -ggdb -fsanitize=address,undefined`. Use
  this while developing. AddressSanitizer finds out-of-bounds access and
  use-after-free, and also reports leaks when the program exits.
- **Release** (`make static`, `make shared`): `-Wall -Wextra -O2 -fPIC`. No
  sanitizers, so it is fast, and `-fPIC` makes the objects usable in a shared
  library. Use this build for timing and for linking into other projects.

Header changes are tracked (`-MMD -MP` makes the compiler write `.d`
dependency files), so editing `hashmap.h` rebuilds everything that includes it.

Run the tests with:

```
make && ./build/test
```

## Using the library

The map stores **bytes**. A key is a pointer and a length. A value is
always exactly `value_size` bytes, a size you choose once, when you create the
map. Both are copied in, so the caller's variables can change or go out of
scope afterwards.

### Quick example: counting words

```c
#include <stdio.h>
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

    printf("to: %d\n", *(int *)hm_get_str(counts, "to"));
    printf("distinct words: %zu\n", hm_count(counts));

    hm_destroy(counts);
    return 0;
}
```

The three `_str` functions are wrappers for NUL-terminated string keys, and
the terminator is not part of the key. For any other key type use
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

### Using the library in another project

There are three ways, from simplest to most "library-like". In every case the
other project includes `"hashmap.h"` and needs `-I<path>/src` so the compiler
finds it.

**1. Copy the two files.** Copy `src/hashmap.h` and `src/hashmap.c` into the
project and compile `hashmap.c` together with your own sources. Nothing to
build or install, and it is the easiest option for pet projects:

```
gcc -Wall -Wextra -Ipath/to/hashmap/src main.c path/to/hashmap/src/hashmap.c -o app
```

**2. Static library.** Run `make static`, then pass the archive file to the
compiler. It is linked into your program, so the program has no runtime
dependency:

```
gcc -Wall -Wextra -Ipath/to/hashmap/src main.c path/to/hashmap/build/release/libskhashmap.a -o app
```

**3. Shared library.** Run `make shared`. The program loads
`libskhashmap.so` when it starts, so the loader has to find it. Either bake
the folder into the program with `-rpath`, or set `LD_LIBRARY_PATH` when
running it:

```
gcc -Wall -Wextra -Ipath/to/hashmap/src main.c \
    -Lpath/to/hashmap/build/release -lskhashmap \
    -Wl,-rpath,/absolute/path/to/hashmap/build/release -o app
```

One trap: with `-lskhashmap`, the linker picks the **shared** library when
both `.a` and `.so` exist in the same folder. To force the static one, pass
the `.a` path directly (option 2), or write
`-Wl,-Bstatic -lskhashmap -Wl,-Bdynamic`, or only build the one you want. If
a program built this way fails at start with "cannot open shared object file",
that's the cause.

Optionally, copy `hashmap.h` and the library file into a shared folder such
as `~/.local/include` and `~/.local/lib`, and then other projects only need
`-I$HOME/.local/include -L$HOME/.local/lib -lskhashmap`.

## API reference

All declarations are in `src/hashmap.h`, with a comment on each function.

| Function | What it does |
|---|---|
| `hm_create(value_size, buckets_count, hash)` | Create an empty map. Both sizes must be greater than 0. `hash` may be `NULL` for the default. Returns `NULL` on invalid arguments or allocation failure. |
| `hm_destroy(hm)` | Free every entry, the bucket array, and the map. `hm_destroy(NULL)` does nothing. |
| `hm_put(hm, key, key_len, value)` | Store a copy of `value` (`value_size` bytes) under the key. If the key exists, only its value is replaced. Returns a pointer to the stored value, or `NULL` if allocation failed (the map is unchanged). |
| `hm_get(hm, key, key_len)` | Pointer to the stored value, or `NULL` if the key is not present. |
| `hm_delete(hm, key, key_len)` | Remove the entry. Returns `true` if it existed. |
| `hm_count(hm)` | Number of entries. 0 for a `NULL` map. |
| `hm_put_str`, `hm_get_str`, `hm_delete_str` | The same three operations for NUL-terminated string keys. |

Rules that go with the API:

- **Keys are equal** if they have the same length and the same bytes. `key`
  must be non-`NULL` even when `key_len` is 0 (the empty key `""` is allowed).
- **Returned pointers** point at the map's own copy of the value. Writing
  through one changes the stored value (`*p += 1` works). A pointer is valid
  until the next `hm_put`, `hm_delete`, or `hm_destroy`, so copy the value out
  if you need it longer.
- **Missing keys** are reported as `NULL`, so any value, including 0, is a
  legal value.
- **A custom hash function** (type `HmHashFn`) must return the same value for
  equal keys and depend on nothing but the bytes it is given.

## How it works

### Data structure

```
Hashmap
  buckets ──► [0] ──► NULL
              [1] ──► Node ──► Node ──► NULL
              [2] ──► Node ──► NULL
              [3] ──► NULL
  buckets_count   number of slots in the array above
  count           number of entries stored
  value_size      bytes per value, fixed at creation
  hash            hash function (default: djb2)

Node
  key, key_len    heap copy of the key bytes, and their number
  value           heap copy of the value bytes
  next            next node in the same bucket, NULL at the end
```

- `buckets` is a `Node **`: an array of `buckets_count` pointers. Each one is
  the head of a linked list (a *chain*) or `NULL` for an empty bucket.
- A key goes to bucket `hash(key) % buckets_count`. Different keys can land in
  the same bucket, and that is the normal case, so every node stores its own
  copy of the key, and a lookup compares keys with the hash only choosing
  where to look.
- Each entry is three separate allocations: the node, the key bytes, and the
  value bytes. Because the value has its own `malloc`, a pointer to it is
  suitably aligned for any type.

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

## Limitations

**Design limits**

- **One value type and size per map.** The size is fixed at creation. For
  different value types, use a map per type, or store a pointer.
- **Pointer values are not managed.** The map copies the bytes of a value, so
  if the value is a pointer to heap memory, the map never frees what it points
  to, not on delete, not on overwrite, and not on destroy. There is no
  per-value free callback. You must free those objects yourself.
- **No iteration.** There is no way to walk all entries, or to list keys.
- **No shrinking.** The table only grows. Deleting entries does not reduce the
  bucket count until the map is destroyed.
- **Not thread-safe.** Don't use one map from several threads without your own
  locking.
- **Returned pointers can go stale.** Only "valid until the next `hm_put`,
  `hm_delete`, or `hm_destroy`" is promised. In this chaining implementation a
  pointer to a value actually survives a resize, but don't depend on it, since
  another implementation (open addressing, for instance) could not keep it.
- **Resize failures are silent.** If growing the table fails, the map carries
  on slower, with no way for the caller to know.

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

`src/test.c` has its own `main()`. It is built into `build/test` by `make`,
with the sanitizers on, and run with `./build/test`. Each test prints its name
and `ok`, and a failing assertion prints the file and line, and the last test
name shown is the one that failed.

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
checked by reading the code), and the default hash function directly, since it
is `static`.

`test.c` refuses to compile with `-DNDEBUG`, because that removes every
`assert` and the tests would pass without checking anything.

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

## Possible next steps

- an iterator, so all entries can be visited
- a benchmark (inserts, hits, misses, deletes, different load factors, good and
  bad hash), with the results saved in this README
- an optional free callback for values that own memory
- an open-addressing version behind the same API, compared with this one on
  the benchmark
- typed convenience wrappers (macros or `static inline` functions) for the key
  and value types used most
