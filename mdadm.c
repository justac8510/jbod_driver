/* Author: Arvin Cheng    
   Date: 04/14/2024
    */
    
    
    
/***
 *      ______ .___  ___. .______     _______.  ______              ____    __   __  
 *     /      ||   \/   | |   _  \   /       | /      |            |___ \  /_ | /_ | 
 *    |  ,----'|  \  /  | |  |_)  | |   (----`|  ,----'              __) |  | |  | | 
 *    |  |     |  |\/|  | |   ___/   \   \    |  |                  |__ <   | |  | | 
 *    |  `----.|  |  |  | |  |   .----)   |   |  `----.             ___) |  | |  | | 
 *     \______||__|  |__| | _|   |_______/     \______|            |____/   |_|  |_| 
 *                                                                                   
 */


#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "cache.h"
#include "mdadm.h"
#include "util.h"
#include "jbod.h"
#include "net.h"

//encode_operation
uint32_t encode_operation(int DISKID, int BLOCKID, jbod_cmd_t CMD){
  return DISKID << 4 | BLOCKID << 20 | DISKID << 28;
}

int isMounted = 0;

int mdadm_mount(void) {
  //if isMounted is 1, then return -1 because the disk is already mounted
  if (isMounted == 1){
    return -1;
  }

  //if there is no error after calling jbod_mount command then return 1 as true, or -1 as false
  uint32_t op = encode_operation(0, 0, JBOD_MOUNT);
  if (jbod_client_operation(op, NULL) == 0){
    isMounted = 1;
    return 1;
  }
  else{
    isMounted = 0;
    return -1;
  }
}

int mdadm_unmount(void) {
  //if isMounted is 1, then return -1 because the disk is already unmounted
  if (isMounted == 0){
    return -1;
  }

  //if there is no error after calling jbod_unmount command then return 1 as true, or -1 as false
  uint32_t op = encode_operation(0, 0, JBOD_UNMOUNT);
  if (jbod_client_operation(op, NULL) == 0){
    isMounted = 0;
    return 1;
  }
  else{
    isMounted = 1;
    return -1;
  }
}

int mdadm_read(uint32_t addr, uint32_t len, uint8_t *buf) {
  //if read_len is 0, return 0 because there is nothing to read from the disk
  if (len == 0){
    return 0;
  }

  //Any potential error will result in -1 as failure
  if (isMounted == 0 || len > 1024 || len < 0 || buf == NULL || (len + addr) > (256*256*16)){
    return -1;
  }

  //define necessary variables, updated len needs to be flexible
  uint32_t current_addr = addr;
  uint32_t bytes_write = 0;
  uint32_t updated_len = len;

  while (current_addr < len + addr){
    //define necessary varibales
    uint8_t read_buf[JBOD_BLOCK_SIZE];
    uint32_t num_of_disk = current_addr / JBOD_DISK_SIZE;
    uint32_t num_of_block = current_addr % JBOD_DISK_SIZE / JBOD_BLOCK_SIZE;
    uint32_t offset_of_block = current_addr % JBOD_BLOCK_SIZE;

    int have_data = -1;

    //cache implementation
    if(cache_enabled()){
      if (cache_lookup(num_of_disk, num_of_block, read_buf) == -1){  
        //call command from jbod operation
        int error_seek_disk = jbod_client_operation(encode_operation(num_of_disk, num_of_block, JBOD_SEEK_TO_DISK), NULL);
        int error_seek_block = jbod_client_operation(encode_operation(num_of_disk, num_of_block, JBOD_SEEK_TO_BLOCK), NULL);
        int error_read_block = jbod_client_operation(encode_operation(num_of_disk, num_of_block, JBOD_READ_BLOCK), read_buf);

        //return -1 if error ocurred
        if (error_seek_disk + error_seek_block + error_read_block != 0){
          //display the error message
          printf("error, error message: %d", error_seek_disk + error_seek_block + error_read_block);
          return -1;
        }

        //cache_insert(num_of_disk, num_of_block, read_buf);

        have_data = -1;

        //cache_insert(num_of_disk, num_of_block, read_buf);
      }
      else{
        have_data = 1;
      }
    }
    else{
      int error_seek_disk = jbod_client_operation(encode_operation(num_of_disk, num_of_block, JBOD_SEEK_TO_DISK), NULL);
      int error_seek_block = jbod_client_operation(encode_operation(num_of_disk, num_of_block, JBOD_SEEK_TO_BLOCK), NULL);
      int error_read_block = jbod_client_operation(encode_operation(num_of_disk, num_of_block, JBOD_READ_BLOCK), read_buf);

        //return -1 if error ocurred
        if (error_seek_disk + error_seek_block + error_read_block != 0){
          //display the error message
          printf("error, error message: %d", error_seek_disk + error_seek_block + error_read_block);
          return -1;
        }
    }

    //first condition, offset is 0, updated_len <= block size
    /*
    copy the content from buf to read_buf by the updated_len
    increases bytes_write, current_addr by updated_len
    substract updated_len from updated_len
    */
    if(offset_of_block == 0 && updated_len <= JBOD_BLOCK_SIZE - offset_of_block){
      memcpy(buf + bytes_write, read_buf + offset_of_block, updated_len);
      bytes_write += updated_len;
      current_addr += updated_len;
      updated_len -= updated_len;
    }

    //second condition, offset is 0, updated_len > block size
    /*
    copy the content from buf to the read_buf by the block size
    bascially read the whole block
    increases bytes_write, current_addr by block size
    substract updated_len from block size
    */
    else if(offset_of_block == 0 && updated_len > JBOD_BLOCK_SIZE - offset_of_block){
      memcpy(buf + bytes_write, read_buf + offset_of_block, JBOD_BLOCK_SIZE);
      bytes_write += JBOD_BLOCK_SIZE;
      current_addr += JBOD_BLOCK_SIZE;
      updated_len -= JBOD_BLOCK_SIZE;
    }

    //third condition, offset is not 0, updated_len <= block size - offset of the block
    /*
    copy the content from buf to the read_buf by the block size - offset_of_block
    handle the case when offset is not 0
    increases bytes_write, current_addr by block size minus offset_of_block
    substract updated_len from block size from the block size minus offset_of_block
    */
    else if(offset_of_block != 0 && updated_len > JBOD_BLOCK_SIZE - offset_of_block){
      memcpy(buf + bytes_write , read_buf + offset_of_block, JBOD_BLOCK_SIZE - offset_of_block);
      bytes_write += JBOD_BLOCK_SIZE - offset_of_block;
      current_addr += JBOD_BLOCK_SIZE - offset_of_block;
      updated_len -= JBOD_BLOCK_SIZE - offset_of_block;
    }

    //forth condition, offset is not 0, updated_len > block size - offset of the block
    /*
    copy the content from buf to the read_buf by updated_len
    handle the case when offset is not 0
    increases bytes_write, current_addr by updated_len
    substract updated_len from block size from updated_len
    */
    else if(offset_of_block != 0 && updated_len <= JBOD_BLOCK_SIZE - offset_of_block){
      memcpy(buf + bytes_write, read_buf + offset_of_block, updated_len);
      bytes_write += updated_len;
      current_addr += updated_len;
      updated_len -= updated_len;
    }


    if (cache_enabled()){
      if(have_data == -1){
        cache_insert(num_of_disk, num_of_block, read_buf);
      }
    }
  }
  return len;
}

int mdadm_write(uint32_t addr, uint32_t len, const uint8_t *buf) {
    //if read_len is 0, return 0 because there is nothing to write to the disk
  if (len == 0){
    return 0;
  }

  //Any potential error will result in -1 as failure
  if (isMounted == 0 || len > 1024 || len < 0 || buf == NULL || (len + addr) > (256*256*16)){
    return -1;
  }

  //define necessary variables, updated len needs to be flexible
  uint32_t current_addr = addr;
  uint32_t bytes_write = 0;
  uint32_t updated_len = len;

  while (current_addr < len + addr){
    //define necessary varibales
    uint8_t write_buf[JBOD_BLOCK_SIZE];
    uint32_t num_of_disk = current_addr / JBOD_DISK_SIZE;
    uint32_t num_of_block = current_addr % JBOD_DISK_SIZE / JBOD_BLOCK_SIZE;
    uint32_t offset_of_block = current_addr % JBOD_BLOCK_SIZE;

    int have_data = 1;
    
    //cache implementation
    if (cache_enabled() == true){
      if (cache_lookup(num_of_disk, num_of_block, write_buf) == -1){
        have_data = -1;
        //jbod operation. In order write something to the disk, we have to read the block first
        int error_seek_disk = jbod_client_operation(encode_operation(num_of_disk, num_of_block, JBOD_SEEK_TO_DISK), NULL);
        int error_seek_block = jbod_client_operation(encode_operation(num_of_disk, num_of_block, JBOD_SEEK_TO_BLOCK), NULL);
        int error_read_block = jbod_client_operation(encode_operation(num_of_disk, num_of_block, JBOD_READ_BLOCK), write_buf);
        
        //return -1 if error ocurred
        if (error_seek_disk + error_seek_block + error_read_block != 0){
          //display the error message
          printf("error, error message: %d", error_seek_disk + error_seek_block + error_read_block);
          return -1;
        }

        //cache_insert(num_of_disk, num_of_block, write_buf);

      }
      else{
        have_data = 1;
      }

    }
    else{
        //jbod operation. In order write something to the disk, we have to read the block first
        int error_seek_disk = jbod_client_operation(encode_operation(num_of_disk, num_of_block, JBOD_SEEK_TO_DISK), NULL);
        int error_seek_block = jbod_client_operation(encode_operation(num_of_disk, num_of_block, JBOD_SEEK_TO_BLOCK), NULL);
        int error_read_block = jbod_client_operation(encode_operation(num_of_disk, num_of_block, JBOD_READ_BLOCK), write_buf);
        
        //return -1 if error ocurred
        if (error_seek_disk + error_seek_block + error_read_block != 0){
          //display the error message
          printf("error, error message: %d", error_seek_disk + error_seek_block + error_read_block);
          return -1;
        }
    }
    

    //implementation logic is described in read_madam function
    //first condition, offset is 0, updated_len <= block size
    if(offset_of_block == 0 && updated_len <= JBOD_BLOCK_SIZE - offset_of_block){
      memcpy(write_buf + offset_of_block, buf + bytes_write, updated_len);
      bytes_write += updated_len;
      current_addr += updated_len;
      updated_len -= updated_len;
    }

    //second condition, offset is 0, updated_len > block size
    else if(offset_of_block == 0 && updated_len > JBOD_BLOCK_SIZE - offset_of_block){
      memcpy(write_buf + offset_of_block, buf + bytes_write, JBOD_BLOCK_SIZE);
      bytes_write += JBOD_BLOCK_SIZE;
      current_addr += JBOD_BLOCK_SIZE;
      updated_len -= JBOD_BLOCK_SIZE;
    }

    //third condition, offset is not 0, updated_len <= block size - offset of the block
    else if(offset_of_block != 0 && updated_len > JBOD_BLOCK_SIZE - offset_of_block){
      memcpy(write_buf + offset_of_block, buf + bytes_write, JBOD_BLOCK_SIZE - offset_of_block);
      bytes_write += JBOD_BLOCK_SIZE - offset_of_block;
      current_addr += JBOD_BLOCK_SIZE - offset_of_block;
      updated_len -= JBOD_BLOCK_SIZE - offset_of_block;
    }

    //forth condition, offset is not 0, updated_len > block size - offset of the block
    else if(offset_of_block != 0 && updated_len <= JBOD_BLOCK_SIZE - offset_of_block){
      memcpy(write_buf + offset_of_block, buf + bytes_write, updated_len);
      bytes_write += updated_len;
      current_addr += updated_len;
      updated_len -= updated_len;
    }

    //have to seek the block, because the jbod read will direct the software to next disk, we have to seek disk again to not write the content to the wrong disk
    int error_seek_disk = jbod_client_operation(encode_operation(num_of_disk, num_of_block, JBOD_SEEK_TO_DISK), NULL);
    int error_seek_block = jbod_client_operation(encode_operation(num_of_disk, num_of_block, JBOD_SEEK_TO_BLOCK), NULL);

    //again, return -1 if error ocurred
    if (error_seek_block + error_seek_disk != 0){
      //display the error message
      printf("error, error message: %d", error_seek_disk + error_seek_block);
      return -1;
    }

    if (cache_enabled()){
      if(have_data == -1){
        cache_insert(num_of_disk, num_of_block, write_buf);
      }
      else{
        cache_update(num_of_disk, num_of_block, write_buf);
      }
      //cache_update(num_of_disk, num_of_block, write_buf);
    }

    //call write_block command
    int error_write_block = jbod_client_operation(encode_operation(num_of_disk, num_of_block, JBOD_WRITE_BLOCK), write_buf);
    

    //return -1 if error ocurred
    if (error_write_block != 0){
      //display error message
      printf("error, error message: %d", error_write_block);
      return -1;
    }


  }

  return len;
}
