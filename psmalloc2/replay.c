#include <stdio.h>

#include "malloc.h"

#include "replaylib.h"

/*
 *  Parses the output from ../mhooks/mhook.c and replays it.
 *
 *  Hopefully in the exact same fashion (but whether our calls to
 *  realloc match the scripts lies in the lap of the malloc gods). 
 *
 *  I guess we will be able to check that (modulo callers) when 
 *  it is finished.
 *
 *  By design it is very unforgiving on the input.
 *
 *  Keep in mind that mhook.c misses stuff at startup, and that the
 *  main idea behind the replay script is to trigger similar bugs
 *  in the client malloc library.
 *
 */


int main(int argc, char* argv[]){
  int code;
  
  if (argc != 2) {
    fprintf(stdout, "Usage: %s <mhook output file>\n", argv[0]);
    return 1;
  }

  code = process_file(argv[1], true);
  
  malloc_stats();


  return code;
}
