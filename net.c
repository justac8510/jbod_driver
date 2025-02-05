#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <err.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include "net.h"
#include "jbod.h"

/* the client socket descriptor for the connection to the server */
int fd = -1;

/* attempts to read n (len) bytes from fd; returns true on success and false on failure. 
It may need to call the system call "read" multiple times to reach the given size len. 
*/

static bool nread(int fd, int len, uint8_t *buf) {
  if (buf == NULL){
    return false;
  }
  
  int bytes_read = 0;
  
  //implement non-blocking i/o functionality
  while (bytes_read < len){
    int curr_bytes = read(fd, buf + bytes_read, len - bytes_read);
    if (curr_bytes < 0){
      return false;
    }
    else{
      bytes_read += curr_bytes;
    }
  }

  return true;
}


/* attempts to write n bytes to fd; returns true on success and false on failure 
It may need to call the system call "write" multiple times to reach the size len.
*/

static bool nwrite(int fd, int len, uint8_t *buf) {
  if (buf == NULL) {
    return false;
  }
  
  int bytes_write = 0;
  
  //implement non-blocking i/o functionality
  while (bytes_write < len){
    int curr_bytes = write(fd, buf + bytes_write, len - bytes_write);
    if (curr_bytes < 0){
      return false;
    }
    else{
      bytes_write += curr_bytes;
    }
  }

  return true;
}


/* Through this function call the client attempts to receive a packet from sd 
(i.e., receiving a response from the server.). It happens after the client previously 
forwarded a jbod operation call via a request message to the server.  
It returns true on success and false on failure. 
The values of the parameters (including op, ret, block) will be returned to the caller of this function: 

op - the address to store the jbod "opcode"  
ret - the address to store the return value of the server side calling the corresponding jbod_operation function.
block - holds the received block content if existing (e.g., when the op command is JBOD_READ_BLOCK)

In your implementation, you can read the packet header first (i.e., read HEADER_LEN bytes first), 
and then use the length field in the header to determine whether it is needed to read 
a block of data from the server. You may use the above nread function here.  
*/

static bool recv_packet(int sd, uint32_t *op, uint16_t *ret, uint8_t *block) {
  
  //if sd is -1, which means jbod server is not connected, return false
  if (sd == -1){
    return false;
  }

  uint8_t header[8];//first create a header with size 8
  if (!nread(sd, 8, header)){//nread
    return false;
  }

  uint16_t len;//len
  memcpy(&len, &header, sizeof(uint16_t));
  len = ntohs(len);
  
  memcpy(op, &header[2], sizeof(uint32_t));
  *op = ntohl(*op);//op code

  memcpy(ret, &header[6], sizeof(uint16_t));
  *ret = ntohs(*ret);//return code

  if(len == 264){
    if (!nread(sd, 256, block)){//if the len is 264, read the rest of the header
      return false;
    }
    else{
      return true;
    }
  }

  return true;
}



/* The client attempts to send a jbod request packet to sd (i.e., the server socket here); 
returns true on success and false on failure. 

op - the opcode. 
block- when the command is JBOD_WRITE_BLOCK, the block will contain data to write to the server jbod system;
otherwise it is NULL.

The above information (when applicable) has to be wrapped into a jbod request packet (format specified in readme).
You may call the above nwrite function to do the actual sending.  
*/
static bool send_packet(int sd, uint32_t op, uint8_t *block) {
  /*
  assume the size of the header is 8 bytes, so use uint8_t
  0-1 length, 2-5 opcode, 6-7 return code, 8 - 263 block, where needed
  */

  //if sd is -1, which means jbod server is not connected, return false
  if (sd == -1){
    return false;
  }

  uint8_t op_cmd = ((op >> 14) & 0x3f); //getting the op command
  
  if (op_cmd == JBOD_WRITE_BLOCK){
    if(block == NULL){//if the block(buffer) is null, return false
      return false;
    }

    //convert op, len, ret with htons or htonl
    op = htonl(op);
    uint16_t len = 264;
    uint16_t ret = 0;
    len = htons(len);
    ret = htons(ret);

    uint8_t header[264];//create a header with size 8 + 256

    memcpy(&header, &len, sizeof(uint16_t));
    memcpy(&header[2], &op, sizeof(uint32_t));
    memcpy(&header[6], &ret, sizeof(uint16_t));
    memcpy(&header[8], block, 256);//copy len, op, ret, block to header

    return nwrite(sd, 264, header);//nwrite
  }
  else{
    op = htonl(op);//convert op, len, ret with htons or htonl
    uint16_t len = 8;
    uint16_t ret = 0;
    len = htons(len);
    ret = htons(ret);

    uint8_t header[8];//create a header with size 8

    memcpy(&header, &len, sizeof(uint16_t));
    memcpy(&header[2], &op, sizeof(uint32_t));
    memcpy(&header[6], &ret, sizeof(uint16_t));//copy len, op, ret, block to header

    return nwrite(sd, 8, header);//nwrite
  }
}




/* attempts to connect to server and set the global fd variable to the
 * socket; returns true if successful and false if not. 
 * this function will be invoked by tester to connect to the server at given ip and port.
 * you will not call it in mdadm.c
*/
bool jbod_connect(const char *ip, uint16_t port) {
  struct sockaddr_in s_addr;//jbod_connect, copied from presentation

  s_addr.sin_family = AF_INET;
  s_addr.sin_port = htons(port);
  if (inet_aton(ip, &s_addr.sin_addr) == 0){
    return false;
  }

  //create a socket
  fd = socket(PF_INET, SOCK_STREAM, 0);

  if (fd == -1){
    //printf("Socket creation failed");
    return false;
  }

  if( connect(fd, (const struct sockaddr *)&s_addr, sizeof(s_addr)) == -1){
    //printf("Error on socket connect");
    return false;
  }

  return true;
}



/* disconnects from the server and resets fd */
void jbod_disconnect(void) {
  if (fd != -1){
    close(fd);
  }
  fd = -1;
  //printf("Have not established the connection");
}


/* sends the JBOD operation to the server (use the send_packet function) and receives 
(use the recv_packet function) and processes the response. 

The meaning of each parameter is the same as in the original jbod_operation function. 
return: 0 means success, -1 means failure.
*/


int jbod_client_operation(uint32_t op, uint8_t *block) {
  uint16_t ret;//return code
  uint32_t r_op;//op code
  
  if (fd == -1){//not connect jbod server
    return -1;
  }

  //send the packet, return -1 if failure
  if (send_packet(fd, op, block) == false){
    return -1;
  }

  //recive the packet, return -1 if failure
  if (recv_packet(fd, &r_op, &ret, block) == false){
    return -1;
  }
  else{
    if (ret == -1){//if the return code is -1, this means the operation is not successful
      return -1;
    }
  }

  //if the operation is successful, return 0
  return 0;
}

