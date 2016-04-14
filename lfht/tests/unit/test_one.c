#include "lfht.h"


uint32_t max = 16 * 4096;

static lfht_t ht;


int main(int argc, char* argv[]){

  bool success;


  success = init_lfht(&ht, max);





  success = delete_lfht(&ht);




}


