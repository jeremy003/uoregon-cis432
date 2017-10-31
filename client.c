#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>		    /*  socket definitions        	*/
#include <sys/types.h>        	/*  socket types              	*/
#include <arpa/inet.h>        	/*  inet (3) funtions         	*/
#include <unistd.h>           	/*  misc. UNIX functions      	*/

#include "raw.h"
#include "duckchat.h"

#define MAX_BUF 1024

int main(int argc, char** argv) {
  int quit = 0;
  char* active_channel = (char*) malloc(sizeof(char) * CHANNEL_MAX);
  char* say_message = (char*) malloc(sizeof(char) * SAY_MAX);
  char buffer[MAX_BUF];


  if (raw_mode) {
  	  printf("Raw mode successful\n");
  } else {
	  printf("Raw mode unsuccessful\n");
  }

  // Check for correct number of arguments
  if (argc != 4) {
	  printf("Usage: ./client <hostname> <port number> <username>\n");
	  cooked_mode;
	  exit(-1);
  }

  // Check username length
  char* username = strdup(argv[3]);
  if (strlen(username) > USERNAME_MAX) {
	  printf("Username too long\n");
	  cooked_mode;
	  exit(-1);
  }

  char* ptr;
  char* hostname = strdup(argv[1]);
  int port = strtol(argv[2], &ptr, 10);

  //printf("Hostname: %s\nPort: %d\nUsername: %s\n", hostname, port, username);

  int c_socket = socket(AF_INET, SOCK_DGRAM, 0);
  if (c_socket < 0) {
	  fprintf(stderr, "Error: Failed to create listening socket.\n");
	  exit(EXIT_FAILURE);
  }

  // Create socket and connect to server
  struct sockaddr_in servaddr;
  memset(&servaddr, 0, sizeof(servaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_port = htons(port);

  if (inet_aton(hostname, &servaddr.sin_addr) <= 0) {
	  fprintf(stderr, "Error: Invalid IP address.\n");
	  exit(EXIT_FAILURE);
  }

  if (connect(c_socket, (struct sockaddr*) &servaddr, sizeof(servaddr)) < 0) {
	  fprintf(stderr, "Error: Failed to connect.\n");
	  exit(EXIT_FAILURE);
  }

  // Send a login request to the server
  struct request_login login;
  login.req_type = REQ_LOGIN;
  strcpy(login.req_username, username);

  send(c_socket, (void*) &login, sizeof(login), 0);

  // Join common channel by default
  active_channel = "Common";
  struct request_join join;
  join.req_type = REQ_JOIN;
  strcpy(join.req_channel, active_channel);

  send(c_socket, (void*) &join, sizeof(join), 0);
  int byte_count;
  int i;
  int len;
  int size;
  char input[SAY_MAX];
  char user_command[SAY_MAX];

  // Create generic message to cast from buffer
  struct text* message;
  // Specific packets received from server once the text type has been decifered
  struct text_say* say;
  struct text_list* list;
  struct text_who* who;
  struct text_error* error;

  // Alternate between prompting user for input and trying to receive packets from server
  while (quit != 1) {
    // Check for input from user
    printf(">");
    scanf("%s", &input);

    // Check for command signal
    if (input[0] == '/') {
      // Count characters after '/'
      len = strlen(input);
      size = 0;
      for (i = 1; i < len; i++) {
        if (input[i] != ' ') {
          size++;
        } else {
          break;
        }
      }
      memcpy(user_command, &input[1], size);
      user_command[size] = '\0';
    }



	  // Receive packet from server
	  byte_count = recv(c_socket, &buffer, sizeof(buffer), 0);
	  message = (struct text*) &buffer;
	  printf("packet received: %d\n", message->txt_type);

	  // Decode packet based on txt_type
	  switch(message->txt_type) {
		  case TXT_SAY:
			  say = (struct text_say*) &buffer;
			  //memcpy(&say, buffer, sizeof(say));
			  printf("[%s][%s] %s\n", say->txt_channel, say->txt_username, say->txt_text);


			  break;

		  case TXT_LIST:
        list = (struct text_list*) &buffer;
        printf("Existing channels:\n");

        // Iterate through the channels and list each one
        len = list->txt_nchannels;
        for (i = 0; i < len; i++) {
          printf("\t%s", list->txt_channels[i].ch_channel);
        }

			  break;

		  case TXT_WHO:
			  break;

		  default:
			  printf("unknown packet\n");
			  break;

	  }



  }


  return 0;
}
