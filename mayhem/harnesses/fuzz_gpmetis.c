/*
 * fuzz_gpmetis — the gpmetis pipeline: graph file -> ReadGraph -> METIS_PartGraphKway or
 * METIS_PartGraphRecursive -> GPReportResults (partition statistics), as programs/gpmetis.c main.
 * File-input target (afl: true): ReadGraph exit()s on malformed input. Option bytes: metis_fuzz.h.
 * Unlike the CLI, nparts may be 1 (the API accepts it; upstream fix 0f8c9af handled nparts==1).
 * gpmetis.c is linked with -Dmain=gpmetis_cli_main so its GPReportResults is reused.
 */
#include "metisbin.h"
#include "metis_fuzz.h"

int main(int argc, char *argv[])
{
  static const idx_t iptypes[] = {-1, METIS_IPTYPE_GROW, METIS_IPTYPE_RANDOM, METIS_IPTYPE_METISRB};
  static const idx_t rtypes[]  = {-1, METIS_RTYPE_FM, METIS_RTYPE_GREEDY};
  unsigned char ob[MF_NOPT];
  idx_t options[METIS_NOPTIONS], objval = 0, *part;
  params_t params;
  graph_t *graph;
  int status = METIS_ERROR;

  if (argc != 2) {
    fprintf(stderr, "usage: %s <graphfile>\n", argv[0]);
    return 1;
  }
  mf_option_bytes(argv[1], ob);

  memset(&params, 0, sizeof(params));
  params.filename = gk_strdup(argv[1]);
  params.nparts   = 1 + ob[0] % 16;
  params.ptype    = (ob[1] & 1) ? METIS_PTYPE_RB : METIS_PTYPE_KWAY;
  params.objtype  = (ob[2] & 1) ? METIS_OBJTYPE_VOL : METIS_OBJTYPE_CUT;
  params.ctype    = (ob[3] & 1) ? METIS_CTYPE_RM : METIS_CTYPE_SHEM;
  params.iptype   = iptypes[ob[4] % 4];
  params.rtype    = rtypes[ob[5] % 3];
  params.no2hop   = (ob[6] >> 0) & 1;
  params.contig   = (ob[6] >> 1) & 1;
  params.minconn  = (ob[6] >> 2) & 1;
  params.dropedges= (ob[6] >> 3) & 1;
  params.ufactor  = ob[7] ? ob[7] : -1;
  params.niter    = ob[8] ? 1 + ob[8] % 20 : 10;
  params.ncuts    = 1 + ob[9] % 3;
  params.niparts  = ob[10] ? ob[10] % 9 : -1;
  params.seed     = ob[11] ? ob[11] : -1;
  params.nooutput = 1;

  graph = ReadGraph(&params);

  /* Library contract (METIS manual, "Graph data structure"): the API requires a symmetric
   * adjacency structure without self-loops or duplicate edges and does not check it; graphchk's
   * own validator CheckGraph() is how a caller checks that. Contract violations are not bugs. */
  if (!CheckGraph(graph, 0, 0)) {
    FreeGraph(&graph);
    gk_free((void **)&params.filename, LTERM);
    return 0;
  }

  ReadTPwgts(&params, graph->ncon);

  if (params.contig && !IsConnected(graph, 0))
    params.contig = 0;

  if (params.iptype == -1 && params.ptype == METIS_PTYPE_RB)
    params.iptype = (graph->ncon == 1 ? METIS_IPTYPE_GROW : METIS_IPTYPE_RANDOM);

  part = imalloc(graph->nvtxs, "main: part");

  METIS_SetDefaultOptions(options);
  options[METIS_OPTION_OBJTYPE]   = params.objtype;
  options[METIS_OPTION_CTYPE]     = params.ctype;
  options[METIS_OPTION_IPTYPE]    = params.iptype;
  options[METIS_OPTION_RTYPE]     = params.rtype;
  options[METIS_OPTION_NO2HOP]    = params.no2hop;
  options[METIS_OPTION_ONDISK]    = 0;
  options[METIS_OPTION_DROPEDGES] = params.dropedges;
  options[METIS_OPTION_MINCONN]   = params.minconn;
  options[METIS_OPTION_CONTIG]    = params.contig;
  options[METIS_OPTION_SEED]      = params.seed;
  options[METIS_OPTION_NIPARTS]   = params.niparts;
  options[METIS_OPTION_NITER]     = params.niter;
  options[METIS_OPTION_NCUTS]     = params.ncuts;
  options[METIS_OPTION_UFACTOR]   = params.ufactor;
  options[METIS_OPTION_DBGLVL]    = 0;

  gk_malloc_init();
  if (params.ptype == METIS_PTYPE_RB)
    status = METIS_PartGraphRecursive(&graph->nvtxs, &graph->ncon, graph->xadj,
                 graph->adjncy, graph->vwgt, graph->vsize, graph->adjwgt,
                 &params.nparts, params.tpwgts, NULL, options, &objval, part);
  else
    status = METIS_PartGraphKway(&graph->nvtxs, &graph->ncon, graph->xadj,
                 graph->adjncy, graph->vwgt, graph->vsize, graph->adjwgt,
                 &params.nparts, params.tpwgts, NULL, options, &objval, part);
  params.maxmemory = gk_GetMaxMemoryUsed();
  gk_malloc_cleanup(0);

  if (graph->adjwgt == NULL)
    graph->adjwgt = ismalloc(graph->nedges, 1, "adjwgt");

  if (status == METIS_OK)
    GPReportResults(&params, graph, part, objval);

  FreeGraph(&graph);
  gk_free((void **)&part, &params.filename, &params.tpwgts, LTERM);
  return 0;
}
