/*
 * fuzz_graphchk — the graphchk pipeline: graph file -> ReadGraph -> CheckGraph, and on a graph
 * CheckGraph rejects, FixGraph -> WriteGraph (to /dev/null instead of the CLI's optional output
 * file), as programs/graphchk.c main (with unit edge weights supplied first, see below). No validity gate: malformed graphs are exactly what the
 * checker is for. File-input target (afl: true): ReadGraph exit()s on malformed input.
 */
#include "metisbin.h"

int main(int argc, char *argv[])
{
  graph_t *graph, *fgraph;
  params_t params;

  if (argc != 2) {
    fprintf(stderr, "usage: %s <graphfile>\n", argv[0]);
    return 1;
  }

  memset(&params, 0, sizeof(params));
  params.filename = gk_strdup(argv[1]);

  graph = ReadGraph(&params);
  if (graph->nvtxs == 0) {
    printf("Empty graph!\n");
    return 0;
  }

  if (CheckGraph(graph, 1, 1)) {
    printf("   The format of the graph is correct!\n");
  }
  else {
    printf("   The format of the graph is incorrect!\n");
    /* FixGraph's precondition is ASSERT(adjwgt != NULL) (libmetis/checkgraph.c); graphchk.c passes
     * an unweighted graph's NULL adjwgt anyway and segfaults (mayhem/graphchk/known-findings/).
     * Supply unit edge weights the way gpmetis.c/ndmetis.c do before using adjwgt. */
    if (graph->adjwgt == NULL)
      graph->adjwgt = ismalloc(graph->nedges, 1, "adjwgt");
    fgraph = FixGraph(graph);
    WriteGraph(fgraph, "/dev/null");
    FreeGraph(&fgraph);
  }

  FreeGraph(&graph);
  gk_free((void **)&params.filename, LTERM);
  return 0;
}
