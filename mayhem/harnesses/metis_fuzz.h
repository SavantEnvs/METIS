/*
 * metis_fuzz.h — shared helpers for the METIS file-input fuzz targets (mayhem/harnesses/fuzz_*.c).
 *
 * Every target takes ONE input file: a METIS graph file (programs/io.c ReadGraph) or mesh file
 * (ReadMesh), exactly as the matching CLI (gpmetis, ndmetis, mpmetis, m2gmetis, graphchk) reads it.
 * The file is handed to the upstream reader unchanged.
 *
 * Option bytes: the METIS formats treat a line starting with '%' as a comment, and the readers skip
 * it. If the file's FIRST line starts with '%', the bytes after the '%' on that line choose the
 * values the CLI would take from its command line (nparts, ptype, ctype, ...). Each byte is read
 * relative to '@' (0x40), so '@' is 0, 'A' is 1, ... and seeds stay printable text: a NUL byte
 * would make ReadMesh's line count (gk_getfilestats) stop early. The reader itself
 * still sees and skips that comment line, so the option line is part of the fuzzed input, not
 * a side channel. A file without such a line runs with option bytes of 0 (see each target).
 *
 * These are file-input targets (Mayhemfile `afl: true`, `@@`) because ReadGraph/ReadMesh call
 * exit() on malformed input: each input is its own process, so that exit() is harmless.
 */
#ifndef METIS_FUZZ_H
#define METIS_FUZZ_H

#include <stdio.h>
#include <string.h>

#define MF_NOPT 16

/* Fill ob[0..MF_NOPT) with the bytes following a leading '%' on the file's first line, minus '@'
 * (0 past the end of that line, or everywhere when the first line is not a '%' comment). */
static void mf_option_bytes(const char *path, unsigned char ob[MF_NOPT])
{
  FILE *fp;
  int c, i;

  memset(ob, 0, MF_NOPT);
  if ((fp = fopen(path, "rb")) == NULL)
    return;
  if (fgetc(fp) == '%') {
    for (i = 0; i < MF_NOPT; i++) {
      c = fgetc(fp);
      if (c == EOF || c == '\n')
        break;
      ob[i] = (unsigned char)(c - '@');
    }
  }
  fclose(fp);
}

#endif
