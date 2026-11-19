# METIS_PartGraphKway aborts when METIS_OPTION_NIPARTS is 0

Reproducer: `niparts0-grid4.graph` (option line `%C@@@@@@@@@I`: k-way, nparts 4, niparts 0, on a 4x4
grid). Upstream CLI: `gpmetis -niparts=0 graphs/4elt.graph 4` aborts the same way.

- **Cause:** `SetupCtrl` (`libmetis/options.c`) takes `METIS_OPTION_NIPARTS` unchecked (only `-1`
  means "default"); `InitKWayPartitioning` (`libmetis/kmetis.c`) passes it as `METIS_OPTION_NCUTS` to
  the nested `METIS_PartGraphRecursive`, whose `CheckParams` rejects `ncuts <= 0`. The nested call
  returns `METIS_ERROR_INPUT`, `gk_errexit(SIGERR, "Failed during initial partitioning")` longjmps out
  of the outer call, and GKlib's memory-core cleanup then fails with
  `Could not find pointer ... in mcore` and raises SIGABRT.
- **Impact:** process abort (denial of service) for any caller passing `niparts = 0`, instead of a
  `METIS_ERROR_INPUT` return.
- **Fix (one line):** in `CheckParams` for `METIS_OP_KMETIS`, reject `ctrl->nIparts == 0`
  (or `< -1`) with `METIS_ERROR_INPUT`, as it already does for `ncuts <= 0`.
