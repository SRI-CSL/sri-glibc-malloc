#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>


const size_t buffersz = 1024;

typedef unsigned char uchar;

static bool readline(FILE* fp, uchar* buffer, size_t buffersz);
  
static bool replayline(uchar* buffer, size_t buffersz);


int main(int argc, char* argv[]){
  FILE* fp;
  uchar buffer[buffersz  + 1];
  size_t linecount;

  if(argc != 2){
    fprintf(stdout, "Usage: %s <mhook output file>\n", argv[0]);
    return 1;
  }

  buffer[buffersz] = '\0';   /* this should never be touched again  */
  

  fp = fopen(argv[1], "r");
  if(fp == NULL){
    fprintf(stderr, "Could not open %s: %s\n", argv[1], strerror(errno));
    return 1;
  }

  linecount = 0;
  
  while(readline(fp, buffer, buffersz)){
    
    if(!replayline(buffer, buffersz)){
      fprintf(stderr, "Replaying line %zu failed: %s\n", linecount, buffer);
      return 1;
    } else {
      linecount++;
    }
  }

  
  fclose(fp);

  fprintf(stdout, "replayed %zu lines from  %s\n", linecount, argv[0]);

  return 0;
}



static bool readline(FILE* fp, uchar* buffer, size_t buffersz){
  size_t index;
  int c;
  
  memset(buffer, '\0', buffersz);

  for(index = 0; index < buffersz; index++){

    c = fgetc(fp);

    if(c == EOF){
      return false;
    }
    
    if(c == '\n'){
      return true;
    }

    buffer[index] = (uchar)c;

  }

  return false;
}

static bool replayline(uchar* buffer, size_t buffersz){

  fprintf(stderr, "%s\n", buffer);
	  
  return true;
}
