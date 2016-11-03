/*
 * Copyright (C) 2016  SRI International
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <stdio.h>

#include "malloc.h"

#include "replaylib.h"

static const bool verbose = true;

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

  code = process_file(argv[1], verbose);
  
  if (verbose) {
    malloc_stats();
  }

  return code;
}
