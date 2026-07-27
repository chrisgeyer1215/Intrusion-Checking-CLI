# RBTree Algorithm Introduction

A red-black tree (RBTree) is a self-balancing binary search tree. It keeps keys
in sorted order while limiting the tree height, so lookup, insertion, and
deletion remain efficient even when keys arrive in an unfavorable order.

This repository uses an RBTree implementation in
[`libinotifytools/src/redblack.cpp`](libinotifytools/src/redblack.cpp), with its
public interface declared in
[`libinotifytools/src/redblack.h`](libinotifytools/src/redblack.h).

## Why use a red-black tree?

A normal binary search tree can degrade into a linked list when values are
inserted in sorted order. In that case, an operation that is normally fast may
require scanning every node.

A red-black tree prevents this degeneration by assigning each node a color and
repairing the tree with recoloring and rotations after insertions and
deletions. The resulting height is bounded by `2 * log2(n + 1)`, where `n` is
the number of stored nodes.

| Operation | Average | Worst case |
| --- | --- | --- |
| Search | `O(log n)` | `O(log n)` |
| Insert | `O(log n)` | `O(log n)` |
| Delete | `O(log n)` | `O(log n)` |
| Ordered traversal | `O(n)` | `O(n)` |

## Red-black properties

The implementation maintains these invariants:

1. Every node is either red or black.
2. The root is black.
3. Every missing child is represented by a shared black sentinel node.
4. A red node cannot have a red child.
5. Every path from a node to a descendant sentinel contains the same number of
   black nodes.

These rules ensure that the longest root-to-leaf path is no more than twice the
length of the shortest path, which keeps the tree approximately balanced.

## How balancing works

### Insertion

A new key is first inserted using normal binary-search-tree ordering and is
colored red. Coloring it red preserves the black-node count on every path, but
it may create a red parent with a red child.

The insertion repair loop handles that conflict by:

- recoloring the parent, uncle, and grandparent when the uncle is red; or
- applying a left or right rotation, followed by recoloring, when the uncle is
  black.

The root is colored black when repair is complete.

### Deletion

Deletion first removes a node using normal binary-search-tree rules. Removing a
black node can reduce the black-node count on one path, so the deletion repair
logic examines the removed node's sibling and performs recoloring and rotations
until all paths have the same black-node count again.

### Rotations

Rotations change the local shape of the tree without changing key order.

A left rotation promotes a node's right child:

```text
    X                  Y
   / \                / \
  A   Y      ->       X   C
     / \            / \
    B   C          A   B
```

A right rotation is the mirror operation.

## Repository API

The non-customized interface stores pointers to caller-owned values and uses a
comparison callback to order them.

```cpp
#include "redblack.h"

int compare_items(const char* left, const char* right, const void* config);

struct rbtree* tree = rbinit(compare_items, nullptr);
```

The comparison callback must return:

- a negative value when `left` sorts before `right`;
- zero when the keys are equivalent; and
- a positive value when `left` sorts after `right`.

The main operations are:

| Function | Purpose |
| --- | --- |
| `rbinit` | Create an empty tree with a comparator and optional configuration. |
| `rbsearch` | Find a matching key or insert it when absent. |
| `rbfind` | Find an exact key without modifying the tree. |
| `rbdelete` | Remove an exact key and return the stored pointer. |
| `rblookup` | Perform exact, range, predecessor, successor, first, or last lookup. |
| `rbopenlist` / `rbreadlist` / `rbcloselist` | Iterate over values in sorted order. |
| `rbwalk` | Traverse the tree with a callback. |
| `rbdestroy` | Free the tree nodes and tree container. |

`rbsearch` does not replace an existing equivalent value. It returns the
pointer already stored in the tree when the comparator reports equality.

The tree does not copy or free the caller's values. Their lifetime must extend
for as long as the values remain in the tree, and the caller remains responsible
for releasing them when appropriate.

## Lookup modes

`rblookup` supports several modes defined in `redblack.h`:

| Mode | Result |
| --- | --- |
| `RB_LUEQUAL` | Exact match only. |
| `RB_LUGTEQ` | Exact match or the next greater key. |
| `RB_LULTEQ` | Exact match or the next smaller key. |
| `RB_LULESS` | Greatest key strictly less than the supplied key. |
| `RB_LUGREAT` | Smallest key strictly greater than the supplied key. |
| `RB_LUNEXT` | Successor of an existing key. |
| `RB_LUPREV` | Predecessor of an existing key. |
| `RB_LUFIRST` | Smallest key in the tree. |
| `RB_LULAST` | Largest key in the tree. |

The convenience macros `rbmin(tree)` and `rbmax(tree)` return the smallest and
largest stored values.

## How the project uses RBTrees

`libinotifytools` maintains trees for watch descriptors, file identifiers, and
filenames. These indexes allow watch data to be located efficiently without a
linear scan.

The statistics output also creates a temporary RBTree with a comparator based
on an event counter. It inserts the current watches into that tree and reads
them back in sorted order before printing the report.

Relevant code locations include:

- [`libinotifytools/src/inotifytools.cpp`](libinotifytools/src/inotifytools.cpp)
- [`src/inotifywatch.cpp`](src/inotifywatch.cpp)

## Implementation notes

- A shared black sentinel (`RBNULL`) represents missing children and tree
  boundaries.
- Each node stores left, right, and parent pointers, its color, and a pointer to
  the caller's value.
- The implementation allocates tree nodes with `malloc` and releases them with
  `free`.
- No internal synchronization is provided; callers must coordinate concurrent
  access.
- Avoid structurally modifying a tree while iterating with an open `RBLIST`.
