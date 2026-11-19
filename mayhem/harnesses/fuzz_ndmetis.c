/*
 * fuzz_ndmetis — the ndmetis pipeline: graph file -> ReadGraph -> METIS_NodeND ->
 * NDReportResults (symbolic factorization / fill-in, programs/smbfactor.c), as programs/ndmetis.c
 * main. File-input target (afl: true): ReadGraph exit()s on malformed input. Option bytes:
 * metis_fuzz.h. ndmetis.c is linked with -Dmain=ndmetis_cli_main so NDReportResults is reused.
 */
#include "metisbin.h"
#include "metis_fuzz.h"

int main(int argc, char *argv[])
{
  unsigned char ob[MF_NOPT];
  idx_t options[METIS_NOPTIONS], *perm, *iperm;
  params_t params;
  graph_t *graph;
  int status;

  if (argc != 2) {
    fprintf(stderr, "usage: %s <graphfile>\n", argv[0]);
    return 1;
  }
  mf_option_bytes(argv[1], ob);

  memset(&params, 0, sizeof(params));
  params.filename = gk_strdup(argv[1]);
  params.ctype    = (ob[0] & 1) ? METIS_CTYPE_RM : METIS_CTYPE_SHEM;
  params.iptype   = (ob[1] & 1) ? METIS_IPTYPE_EDGE : METIS_IPTYPE_NODE;
  params.rtype    = (ob[2] & 1) ? METIS_RTYPE_SEP2SIDED : METIS_RTYPE_SEP1SIDED;
  params.ufactor  = ob[3] ? ob[3] : OMETIS_DEFAULT_UFACTOR;
  params.pfactor  = ob[4] % 64;
  params.compress = (ob[5] & 1) ? 0 : 1;
  params.ccorder  = (ob[5] >> 1) & 1;
  params.no2hop   = (ob[5] >> 2) & 1;
  params.nseps    = 1 + ob[6] % 3;
  params.niter    = ob[7] ? 1 + ob[7] % 20 : 10;
  params.seed     = ob[8] ? ob[8] : -1;
  params.nparts   = 1;
  params.nooutput = 1;

  graph = ReadGraph(&params);

  /* Library contract (METIS manual, "Graph data structure"): symmetric adjacency, no self-loops,
   * no duplicate edges; the API does not check it. CheckGraph() is upstream's own validator. */
  if (!CheckGraph(graph, 0, 0) || graph->ncon != 1) {   /* ndmetis: ordering needs ncon == 1 */
    FreeGraph(&graph);
    gk_free((void **)&params.filename, LTERM);
    return 0;
  }

  perm  = imalloc(graph->nvtxs, "main: perm");
  iperm = imalloc(graph->nvtxs, "main: iperm");

  METIS_SetDefaultOptions(options);
  options[METIS_OPTION_CTYPE]    = params.ctype;
  options[METIS_OPTION_IPTYPE]   = params.iptype;
  options[METIS_OPTION_RTYPE]    = params.rtype;
  options[METIS_OPTION_DBGLVL]   = 0;
  options[METIS_OPTION_UFACTOR]  = params.ufactor;
  options[METIS_OPTION_NO2HOP]   = params.no2hop;
  options[METIS_OPTION_ONDISK]   = 0;
  options[METIS_OPTION_COMPRESS] = params.compress;
  options[METIS_OPTION_CCORDER]  = params.ccorder;
  options[METIS_OPTION_SEED]     = params.seed;
  options[METIS_OPTION_NITER]    = params.niter;
  options[METIS_OPTION_NSEPS]    = params.nseps;
  options[METIS_OPTION_PFACTOR]  = params.pfactor;

  gk_malloc_init();
  status = METIS_NodeND(&graph->nvtxs, graph->xadj, graph->adjncy, graph->vwgt,
               options, perm, iperm);
  params.maxmemory = gk_GetMaxMemoryUsed();
  gk_malloc_cleanup(0);

  if (graph->adjwgt == NULL)
    graph->adjwgt = ismalloc(graph->nedges, 1, "adjwgt");

  if (status == METIS_OK)
    NDReportResults(&params, graph, perm, iperm);

  FreeGraph(&graph);
  gk_free((void **)&perm, &iperm, &params.filename, LTERM);
  return 0;
}
