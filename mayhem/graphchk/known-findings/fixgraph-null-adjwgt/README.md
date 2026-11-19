# graphchk: NULL `adjwgt` dereference in `FixGraph` for an unweighted malformed graph

Reproducer: `unweighted-asym.graph` (a 4-vertex graph with no edge weights, a duplicate edge and a
self-loop). Upstream CLI: `build/test/programs/graphchk unweighted-asym.graph out.graph` segfaults.

- **Cause:** `programs/graphchk.c` calls `FixGraph(graph)` when `CheckGraph` fails and an output file
  is given. `ReadGraph` leaves `graph->adjwgt == NULL` when the file has no edge weights (fmt `xx0`),
  but `FixGraph` (`libmetis/checkgraph.c`) requires `adjwgt != NULL` (its `ASSERT`, compiled out by
  `-DNDEBUG`) and reads `adjwgt[j]` unconditionally (line 218 onward).
- **Impact:** NULL pointer read (SIGSEGV; UBSan reports "applying zero offset to null pointer" at
  `checkgraph.c:218`) on any malformed unweighted graph passed to `graphchk <in> <out>`.
- **Fix (one line):** in `graphchk.c`, before `FixGraph`, add
  `if (graph->adjwgt == NULL) graph->adjwgt = ismalloc(graph->nedges, 1, "adjwgt");`
  (as `gpmetis.c`/`ndmetis.c` already do), or make `FixGraph` treat a NULL `adjwgt` as unit weights.

The `graphchk` fuzz target supplies unit weights before `FixGraph` (honouring its precondition), so
this reproducer does not crash the target; it is kept here as a candidate POV, not in `testsuite/`.
