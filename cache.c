#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "cache.h"

static cache_entry_t *cache = NULL;
static int cache_size = 0;
static int clock = 0;
static int num_queries = 0;
static int num_hits = 0;

static int is_created = -1;


int cache_create(int num_entries) {
  //declaring the function twice without first calling cache_destroy should fail
  if (cache_enabled()){
    return -1;
  }

  if (num_entries < 2 || num_entries > 4096){
    return -1;
  }

  cache = (cache_entry_t*) calloc(num_entries, sizeof(cache_entry_t));
  cache_size = num_entries;
  is_created = 1;

  return 1;
}

int cache_destroy(void) {
  if (!cache_enabled()){
    return -1;
  }

  free(cache);
  cache = NULL;
  cache_size = 0;
  is_created = -1;

  return 1;
}

int cache_lookup(int disk_num, int block_num, uint8_t *buf) {
  if (cache == NULL || buf == NULL || disk_num >= JBOD_NUM_DISKS || disk_num < 0 || block_num >= JBOD_NUM_BLOCKS_PER_DISK  || block_num < 0){
    return -1;
  }

  num_queries ++;

  for (int index = 0; index < cache_size; index++){
    if (cache[index].disk_num == disk_num && cache[index].block_num == block_num && cache[index].valid == true){
      memcpy(buf, cache[index].block,  JBOD_BLOCK_SIZE);

      num_hits ++;
      clock ++;
      cache[index].access_time = clock;

      return 1;
    }
  }

  return -1;
}

void cache_update(int disk_num, int block_num, const uint8_t *buf) {
  if (buf != NULL && cache != NULL && disk_num < JBOD_NUM_DISKS && disk_num >= 0 && block_num < JBOD_NUM_BLOCKS_PER_DISK  && block_num >= 0){
    for (int index = 0; index < cache_size; index++){
      if (cache[index].disk_num == disk_num && cache[index].block_num == block_num && cache[index].valid == true){
        memcpy(cache[index].block, buf, JBOD_BLOCK_SIZE);

        clock ++;
        cache[index].access_time = clock;
      }
    }
  }
}

int cache_insert(int disk_num, int block_num, const uint8_t *buf) {
  if (buf == NULL || cache == NULL || disk_num >= JBOD_NUM_DISKS || disk_num < 0 || block_num >= JBOD_NUM_BLOCKS_PER_DISK  || block_num < 0){
    //printf("error1");
    return -1;
  }

  //condition: block_exist
  for(int index = 0; index < cache_size; index ++){
    if (cache[index].disk_num == disk_num && cache[index].block_num == block_num && cache[index].valid == true){
      return -1;
    }
  }

  int max = 0;

  //condition: available cache
  for (int index = 0; index < cache_size; index ++){
    if(cache[index].valid == false){
      // printf("condition_2");
      memcpy(cache[index].block, buf, JBOD_BLOCK_SIZE);

      clock++;
      cache[index].access_time = clock;
      cache[index].disk_num = disk_num;
      cache[index].block_num = block_num;
      cache[index].valid = true;

      return 1;
    }

    if(cache[index].access_time < cache[max].access_time){
      //printf("max: %d, access time: %d", max, cache[index].access_time);
      max = index;
    }
  }

  //condition: cache is full
  memcpy(cache[max].block, buf, JBOD_BLOCK_SIZE);

  clock++;
  cache[max].access_time = clock;
  cache[max].disk_num = disk_num;
  cache[max].block_num = block_num;

  return 1;  

  
}

bool cache_enabled(void) {
  if (is_created == 1){
    return true;
  }
  else{
    return false;
  }
}

void cache_print_hit_rate(void) {
  fprintf(stderr, "Hit rate: %5.1f%%\n", 100 * (float) num_hits / num_queries);
}
