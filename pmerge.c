#include <stdio.h>
#include <stdint.h>
#include <sysexits.h>
#include "pcap.h"

struct input_file {
  int active;
  struct pcap_file   p;
  struct pcap_pkthdr next;
};

int
usage(int ret)
{
  printf("Usage: pmerge FILE ...\n");
  printf("\n");
  printf("Merges pcap files, outputting time-ordered pcap stream\n");
  return ret;
}

int
read_next(struct input_file *file)
{
  if (! file->active) return -1;

  if (-1 == pcap_read_pkthdr(&file->p, &file->next)) {
    pcap_close(&file->p);
    file->active = 0;
    file->next.ts.tv_sec = 0xffffffff;
    file->next.ts.tv_usec = 0xffffffff;
    return -1;
  }

  return 0;
}

int
main(int argc, char *argv[])
{
  struct input_file files[argc-1];
  int               nfiles = 0;
  int               nopen;
  int               i, ret;

  if (1 == argc) return usage(0);

  /* Open input files */
  for (i = 0; i < argc-1; i += 1) {
    char              *fn  = argv[i+1];
    struct input_file *cur = &files[nfiles];
    FILE              *f;

    if ('-' == fn[0]) return usage(0);

    f = fopen(fn, "rb");
    if (NULL == f) {
      perror(fn);
      return EX_NOINPUT;
    }

    ret = pcap_open_in(&cur->p, f);
    if (-1 == ret) {
      fprintf(stderr, "%s: unable to process\n", fn);
      return EX_IOERR;
    }
    cur->active = 1;

    if (0 == read_next(cur)) {
      nfiles += 1;
    }
  }

  ret = pcap_write_header(stdout);
  if (-1 == ret) {
    perror("writing header");
    return EX_IOERR;
  }

  nopen = nfiles;
  DUMP_d(nopen);
  while (nopen) {
    struct input_file *cur = &files[0];
    char               frame[MAXFRAME];
    size_t             len;

    /* Find next oldest frame */
    for (i = 0; i < nfiles; i += 1) {
      if (files[i].active &&
          ((files[i].next.ts.tv_sec < cur->next.ts.tv_sec) ||
           ((files[i].next.ts.tv_sec == cur->next.ts.tv_sec) &&
            (files[i].next.ts.tv_usec < cur->next.ts.tv_usec)))) {
        cur = &files[i];
      }
    }

    /* Make sure it'll fit */
    if (cur->next.caplen > sizeof(frame)) {
      fprintf(stderr, "error: huge frame (size %u)\n", len);
      return EX_SOFTWARE;
    }

    /* Read it */
    len = fread(frame, 1, cur->next.caplen, cur->p.f);
    if (len < cur->next.caplen) {
      /* Short read */
      cur->next.caplen = len;
      pcap_close(&cur->p);
      cur->active = 0;
    }

    /* Write header + frame */
    if (len) {
      if (1 != fwrite(&cur->next, sizeof(cur->next), 1, stdout)) {
        perror("error");
        return EX_IOERR;
      }
      if (len != fwrite(frame, 1, len, stdout)) {
        perror("error");
        return EX_IOERR;
      }
    }

    if (-1 == read_next(cur)) {
      nopen -= 1;
    }
  }

  return 0;
}
