#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>		      /*  socket definitions        	*/
#include <sys/types.h>        	/*  socket types              	*/
#include <arpa/inet.h>        	/*  inet (3) funtions         	*/
#include <unistd.h>           	/*  misc. UNIX functions      	*/
#include <netdb.h>              /*  hostent                     */

#include <string>
#include <iostream>
#include <vector>
#include <algorithm>

using namespace std;

#include "raw.h"
#include "duckchat.h"

#define MAX_BUF 1024

// Global variables
vector<string> subscribed_channels;
string active_channel;
int c_socket;
char buffer[MAX_BUF];

// Packets to server
struct request_logout request_logout;
struct request_join request_join;
struct request_leave request_leave;
struct request_say request_say;
struct request_list request_list;
struct request_who request_who;

// Packets from server
struct text* message;
// Specific packets received from server once the text type has been decifered
struct text_say* say;
struct text_list* list;
struct text_who* who;
struct text_error* error;

void handle_user_input(string input);
void handle_socket_input();
int hostname_to_ip(char * hostname , char* ip);

int main(int argc, char** argv) {
  int quit = 0;
  active_channel = "Common";
  subscribed_channels.push_back(active_channel);

  // Check for correct number of arguments
  if (argc != 4) {
	  printf("Usage: ./client <hostname> <port number> <username>\n");
	  exit(-1);
  }

  // Check username length
  char* username = strdup(argv[3]);
  if (strlen(username) > USERNAME_MAX) {
	  printf("Username too long\n");
	  exit(-1);
  }

  char* ptr;
  char ip[100];
  hostname_to_ip(argv[1], ip);
  int port = strtol(argv[2], &ptr, 10);

  c_socket = socket(AF_INET, SOCK_DGRAM, 0);
  if (c_socket < 0) {
	  fprintf(stderr, "Error: Failed to create listening socket.\n");
	  exit(EXIT_FAILURE);
  }

  // Create socket and connect to server
  struct sockaddr_in servaddr;
  memset(&servaddr, 0, sizeof(servaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_port = htons(port);

  if (inet_aton(ip, &servaddr.sin_addr) <= 0) {
	  fprintf(stderr, "Error: Invalid IP address.\n");
	  exit(EXIT_FAILURE);
  }

  if (connect(c_socket, (struct sockaddr*) &servaddr, sizeof(servaddr)) < 0) {
	  fprintf(stderr, "Error: Failed to connect.\n");
	  exit(EXIT_FAILURE);
  }

  // Send a login request to the server
  struct request_login request_login;
  request_login.req_type = REQ_LOGIN;
  strcpy(request_login.req_username, username);

  send(c_socket, (void*) &request_login, sizeof(request_login), 0);

  // Packets to send to server
  request_logout.req_type = REQ_LOGOUT;
  request_join.req_type = REQ_JOIN;
  request_leave.req_type = REQ_LEAVE;
  request_say.req_type = REQ_SAY;
  request_list.req_type = REQ_LIST;
  request_who.req_type = REQ_WHO;

  strcpy(request_join.req_channel, active_channel.c_str());

  send(c_socket, (void*) &request_join, sizeof(request_join), 0);

  string input;
  fd_set rset;
  ssize_t n;
  FD_ZERO(&rset);
  // Alternate between prompting user for input and trying to receive packets from server
  while (true) {
    FD_SET(c_socket, &rset);
    FD_SET(0, &rset);

    int maxfpd1 = max(c_socket, 0) + 1;
    select(maxfpd1, &rset, NULL, NULL, NULL);

    if(FD_ISSET(0, &rset)){
      getline(cin, input);
      handle_user_input(input);
    }
    if(FD_ISSET(c_socket, &rset)){
      recv(c_socket, &buffer, sizeof(buffer), 0);
      handle_socket_input();
    }

  }

  return 0;
}


void handle_user_input(string input) {
  string user_command;

  // Check for command signal
  if (input[0] == '/') {
    user_command = input.substr(1, input.find(" ") - 1);

    if (user_command.compare("exit") == 0) {
      send(c_socket, (void*) &request_logout, sizeof(request_logout), 0);
      close(c_socket);
      exit(0);

    } else if (user_command.compare("join") == 0) {
      user_command = input.substr(input.find(" ") + 1);
      strcpy(request_join.req_channel, user_command.c_str());
      send(c_socket, (void*) &request_join, sizeof(request_join), 0);
      subscribed_channels.push_back(user_command);
      active_channel = user_command;

    } else if (user_command.compare("leave") == 0) {
      user_command = input.substr(input.find(" ") + 1);
      strcpy(request_leave.req_channel, user_command.c_str());
      send(c_socket, (void*) &request_leave, sizeof(request_leave), 0);
      subscribed_channels.erase(remove(subscribed_channels.begin(), subscribed_channels.end(), user_command), subscribed_channels.end());

    } else if (user_command.compare("list") == 0) {
      send(c_socket, (void*) &request_list, sizeof(request_list), 0);

    } else if (user_command.compare("who") == 0) {
      user_command = input.substr(input.find(" ") + 1);
      strcpy(request_who.req_channel, user_command.c_str());
      send(c_socket, (void*) &request_who, sizeof(request_list), 0);

    } else if (user_command.compare("switch") == 0) {
      user_command = input.substr(input.find(" ") + 1);
      if (find(subscribed_channels.begin(), subscribed_channels.end(), user_command) != subscribed_channels.end()) {
        // Channel is in subscribed_channels
        active_channel = user_command;

      } else {
        // Channel is not in subscribed_channels
        cout << "You have not subscribed to channel " << user_command << endl;

      }

    } else {
      cout << "*Unknown command" << endl;
    }

  } else {
    // No '/' char so send a message
    strcpy(request_say.req_channel, active_channel.c_str());
    strcpy(request_say.req_text, input.c_str());
    send(c_socket, (void*) &request_say, sizeof(request_say), 0);
  }
}

void handle_socket_input() {
  int i;
  int len;
  message = (struct text*) &buffer;

  // Decode packet based on txt_type
  switch (message->txt_type) {
    case TXT_SAY:
      say = (struct text_say*) &buffer;
      printf("[%s][%s]: %s\n", say->txt_channel, say->txt_username, say->txt_text);
      break;

    case TXT_LIST:
      list = (struct text_list*) &buffer;
      cout << "Existing channels:" << endl;

      len = list->txt_nchannels;
      for (i = 0; i < len; i++) {
        cout << "\t" << list->txt_channels[i].ch_channel << endl;
      }
      cout << endl;
      break;

    case TXT_WHO:
      who = (struct text_who*) &buffer;
      cout << "Users on channel " << who->txt_channel << ":" << endl;

      len = who->txt_nusernames;
      for (i = 0; i < len; i++) {
        cout << "\t" << who->txt_users[i].us_username << endl;
      }
      cout << endl;
      break;

    default:
      printf("unknown packet\n");
      break;

  }
}

// Function from http://www.binarytides.com/hostname-to-ip-address-c-sockets-linux/
int hostname_to_ip(char *hostname , char *ip)
{
    int sockfd;
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_in *h;
    int rv;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC; // use AF_INET6 to force IPv6
    hints.ai_socktype = SOCK_STREAM;

    if ( (rv = getaddrinfo( hostname , "http" , &hints , &servinfo)) != 0)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    // loop through all the results and connect to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next)
    {
        h = (struct sockaddr_in *) p->ai_addr;
        strcpy(ip , inet_ntoa( h->sin_addr ) );
    }

    freeaddrinfo(servinfo); // all done with this structure
    return 0;
}
