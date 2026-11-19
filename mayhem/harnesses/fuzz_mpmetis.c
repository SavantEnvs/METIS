/*
 * fuzz_mpmetis — the mpmetis pipeline: mesh file -> ReadMesh -> METIS_PartMeshDual or
 * METIS_PartMeshNodal -> MPReportResults, as programs/mpmetis.c main. File-input target
 * (afl: true): ReadMesh exit()s on malformed input. Option bytes: metis_fuzz.h.
 * mpmetis.c is linked with -Dmain=mpmetis_cli_main so MPReportResults is reused.
 */
#include "metisbin.h"
#include "metis_fuzz.h"

int main(int argc, char *argv[])
{
  static const idx_t rtypes[] = {-1, METIS_RTYPE_FM, METIS_RTYPE_GREEDY};
  unsigned char ob[MF_NOPT];
  idx_t options[METIS_NOPTIONS], objval = 0, *epart, *npart;
  params_t params;
  mesh_t *mesh;
  int status;

  if (argc != 2) {
    fprintf(stderr, "usage: %s <meshfile>\n", argv[0]);
    return 1;
  }
  mf_option_bytes(argv[1], ob);

  memset(&params, 0, sizeof(params));
  params.filename = gk_strdup(argv[1]);
  params.nparts   = 1 + ob[0] % 16;
  params.gtype    = (ob[1] & 1) ? METIS_GTYPE_NODAL : METIS_GTYPE_DUAL;
  params.ptype    = (ob[2] & 1) ? METIS_PTYPE_RB : METIS_PTYPE_KWAY;
  params.objtype  = (ob[3] & 1) ? METIS_OBJTYPE_VOL : METIS_OBJTYPE_CUT;
  params.ctype    = (ob[3] & 2) ? METIS_CTYPE_RM : METIS_CTYPE_SHEM;
  params.iptype   = (ob[4] & 1) ? METIS_IPTYPE_RANDOM : METIS_IPTYPE_GROW;
  params.rtype    = rtypes[ob[5] % 3];
  params.ncommon  = 1 + ob[6] % 4;
  params.contig   = (ob[7] >> 0) & 1;
  params.minconn  = (ob[7] >> 1) & 1;
  params.no2hop   = (ob[7] >> 2) & 1;
  params.ufactor  = ob[8] ? ob[8] : -1;
  params.niter    = ob[9] ? 1 + ob[9] % 20 : 10;
  params.ncuts    = 1 + ob[10] % 3;
  params.seed     = ob[11] ? ob[11] : -1;
  params.nooutput = 1;

  mesh = ReadMesh(&params);
  if (mesh->ncon > 1) {            /* mpmetis: multi-constraint meshes are not supported */
    FreeMesh(&mesh);
    gk_free((void **)&params.filename, LTERM);
    return 0;
  }
  ReadTPwgts(&params, mesh->ncon);

  epart = imalloc(mesh->ne, "main: epart");
  npart = imalloc(mesh->nn, "main: npart");

  METIS_SetDefaultOptions(options);
  options[METIS_OPTION_PTYPE]   = params.ptype;
  options[METIS_OPTION_OBJTYPE] = params.objtype;
  options[METIS_OPTION_CTYPE]   = params.ctype;
  options[METIS_OPTION_IPTYPE]  = params.iptype;
  options[METIS_OPTION_NO2HOP]  = params.no2hop;
  options[METIS_OPTION_ONDISK]  = 0;
  options[METIS_OPTION_RTYPE]   = params.rtype;
  options[METIS_OPTION_DBGLVL]  = 0;
  options[METIS_OPTION_UFACTOR] = params.ufactor;
  options[METIS_OPTION_MINCONN] = params.minconn;
  options[METIS_OPTION_CONTIG]  = params.contig;
  options[METIS_OPTION_SEED]    = params.seed;
  options[METIS_OPTION_NITER]   = params.niter;
  options[METIS_OPTION_NCUTS]   = params.ncuts;

  gk_malloc_init();
  if (params.gtype == METIS_GTYPE_DUAL)
    status = METIS_PartMeshDual(&mesh->ne, &mesh->nn, mesh->eptr, mesh->eind,
                 mesh->ewgt, NULL, &params.ncommon, &params.nparts,
                 params.tpwgts, options, &objval, epart, npart);
  else
    status = METIS_PartMeshNodal(&mesh->ne, &mesh->nn, mesh->eptr, mesh->eind,
                 NULL, NULL, &params.nparts, params.tpwgts, options, &objval,
                 epart, npart);
  params.maxmemory = gk_GetMaxMemoryUsed();
  gk_malloc_cleanup(0);

  if (status == METIS_OK)
    MPReportResults(&params, mesh, epart, npart, objval);

  FreeMesh(&mesh);
  gk_free((void **)&epart, &npart, &params.filename, &params.tpwgts, LTERM);
  return 0;
}
