/*
 * fuzz_m2gmetis — the m2gmetis pipeline: mesh file -> ReadMesh -> METIS_MeshToDual or
 * METIS_MeshToNodal -> WriteGraph (to /dev/null instead of the CLI's output file) ->
 * M2GReportResults, as programs/m2gmetis.c main. File-input target (afl: true): ReadMesh exit()s
 * on malformed input. Option bytes: metis_fuzz.h. m2gmetis.c is linked with
 * -Dmain=m2gmetis_cli_main so M2GReportResults is reused.
 */
#include "metisbin.h"
#include "metis_fuzz.h"

int main(int argc, char *argv[])
{
  unsigned char ob[MF_NOPT];
  params_t params;
  mesh_t *mesh;
  graph_t *graph;
  int status;

  if (argc != 2) {
    fprintf(stderr, "usage: %s <meshfile>\n", argv[0]);
    return 1;
  }
  mf_option_bytes(argv[1], ob);

  memset(&params, 0, sizeof(params));
  params.filename = gk_strdup(argv[1]);
  params.gtype    = (ob[0] & 1) ? METIS_GTYPE_NODAL : METIS_GTYPE_DUAL;
  params.ncommon  = 1 + ob[1] % 4;
  params.numflag  = 0;

  mesh = ReadMesh(&params);
  if (mesh->ncon > 1) {            /* m2gmetis: multi-constraint meshes are not supported */
    FreeMesh(&mesh);
    gk_free((void **)&params.filename, LTERM);
    return 0;
  }

  graph = CreateGraph();
  gk_malloc_init();
  if (params.gtype == METIS_GTYPE_DUAL) {
    status = METIS_MeshToDual(&mesh->ne, &mesh->nn, mesh->eptr, mesh->eind,
                 &params.ncommon, &params.numflag, &graph->xadj, &graph->adjncy);
    if (status == METIS_OK) {
      graph->nvtxs  = mesh->ne;
      graph->nedges = graph->xadj[graph->nvtxs];
      graph->ncon   = 1;
    }
  }
  else {
    status = METIS_MeshToNodal(&mesh->ne, &mesh->nn, mesh->eptr, mesh->eind,
                 &params.numflag, &graph->xadj, &graph->adjncy);
    if (status == METIS_OK) {
      graph->nvtxs  = mesh->nn;
      graph->nedges = graph->xadj[graph->nvtxs];
      graph->ncon   = 1;
    }
  }
  params.maxmemory = gk_GetMaxMemoryUsed();
  gk_malloc_cleanup(0);

  if (status == METIS_OK) {
    WriteGraph(graph, "/dev/null");
    M2GReportResults(&params, mesh, graph);
  }

  FreeGraph(&graph);
  FreeMesh(&mesh);
  gk_free((void **)&params.filename, LTERM);
  return 0;
}
