#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>        	 /*  inet (3) funtions         	*/
#include <netdb.h>               /*  hostent                     */

#include <string>
#include <iostream>
#include <vector>
#include <algorithm>
#include <map>

using namespace std;

#include "raw.h"
#include "duckchat.h"

#define MAX_BUF 1024

// struct User {
//   string username;
//   vector<string> channels;
// };

class User {
public:
  User() {};
  string username;
  struct sockaddr_in addr;
  vector<string> channels;
};

struct request* c_request;
struct request_login* req_login;
struct request_logout* req_logout;
struct request_join* req_join;
struct request_leave* req_leave;
struct request_say* req_say;
struct request_list* req_list;
struct request_who* req_who;

struct text_say txt_say;
struct text_list* txt_list;
struct text_who txt_who;
struct text_error txt_error;

struct channel_info* ch_info;

int hostname_to_ip(char * hostname , char* ip);

int main(int argc, char* argv[]) {
  // Check for correct number of arguments
  if (argc != 3) {
    cout << "Usage: ./server <hostname> <port number>" << endl;
    exit(EXIT_FAILURE);
  }

  char ip[100];
  hostname_to_ip(argv[1], ip);

  char* ptr;
  int port = strtol(argv[2], &ptr, 10);
  if (port <= 0) {
    cout << "Invalid port number" << endl;
    exit(EXIT_FAILURE);
  }

  txt_say.txt_type = TXT_SAY;
  // txt_list.txt_type = TXT_LIST;
  txt_who.txt_type = TXT_WHO;
  txt_error.txt_type = TXT_ERROR;

  map<int, User> client_map; // maps client port number to User
  map<string, vector<int> > channel_map; // maps channel name to vector of client port numbers
  channel_map["Common"];

  int s_socket;
  char buffer[MAX_BUF];
  char ipstr[INET6_ADDRSTRLEN];
  struct sockaddr_in serv_addr;

  s_socket = socket(AF_INET, SOCK_DGRAM, 0);
  if (s_socket < 0) {
    cout << "Error opening socket" << endl;
    exit(-1);
  }

  memset(&serv_addr, 0, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(port);

  if (inet_pton(AF_INET, ip, &serv_addr.sin_addr) < 1) {
	  fprintf(stderr, "Error: Invalid IP address.\n");
	  exit(EXIT_FAILURE);
  }

  if (bind(s_socket, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) < 0) {
    cout << "Error on binding" << endl;
    exit(-1);
  }

  struct sockaddr_in src_addr;
  struct sockaddr_in tmp_addr;
  socklen_t fromlen = sizeof(src_addr);
  int c_port, i;
  vector<int> ch_vec;
  vector<string> cli_vec;
  map<int, User>::iterator client_map_it;
  map<string, vector<int> >::iterator channel_map_it;

  while(1) {
    recvfrom(s_socket, &buffer, sizeof(buffer), 0, (struct sockaddr*) &src_addr, &fromlen);
    c_request = (struct request*) &buffer;
    // Verify correct socket struct
    if (src_addr.sin_family == AF_INET) {
      switch (c_request->req_type) {
        case REQ_LOGIN:
          req_login = (struct request_login*) &buffer;

          c_port = ntohs(src_addr.sin_port);

          client_map[c_port] = User();
          client_map[c_port].username = req_login->req_username;
          client_map[c_port].addr = src_addr;
          cout << "User " << req_login->req_username << " logged in on port " << c_port << endl;
          break;

        case REQ_LOGOUT:
          req_logout = (struct request_logout*) &buffer;

          c_port = ntohs(src_addr.sin_port);

          // remove user from joined channels
          for (channel_map_it = channel_map.begin(); channel_map_it != channel_map.end(); channel_map_it++) {
            ch_vec = channel_map_it->second;
            ch_vec.erase(remove(ch_vec.begin(), ch_vec.end(), c_port), ch_vec.end());
          }

          cout << "User " << client_map[c_port].username << " logged out" << endl;

          client_map.erase(c_port);

          break;

        case REQ_JOIN:
          req_join = (struct request_join*) &buffer;

          c_port = ntohs(src_addr.sin_port);
          ch_vec = channel_map[req_join->req_channel];

          // look through channel's vector for client port
          if (find(ch_vec.begin(), ch_vec.end(), c_port) == ch_vec.end()) {
            // user is not currently in the channel
            client_map[c_port].channels.push_back(req_join->req_channel); // add channel to user mapping
            channel_map[req_join->req_channel].push_back(c_port); // add user to channel mapping
            cout << "User " << client_map[c_port].username << " has joined channel " << req_join->req_channel << endl;
          } else {
            cout << "User is already in " << req_join->req_channel << endl;
          }

          for (i = 0; i < ch_vec.size(); i++) {
            cout << ch_vec[i] << endl;
          }

          break;

        case REQ_LEAVE:
          req_leave = (struct request_leave*) &buffer;

          c_port = ntohs(src_addr.sin_port);
          ch_vec = channel_map[req_leave->req_channel];

          // remove user from channel's vector of users
          ch_vec.erase(remove(ch_vec.begin(), ch_vec.end(), c_port), ch_vec.end());

          // remove user from user's vector of channels
          cli_vec = client_map[c_port].channels;
          cli_vec.erase(remove(cli_vec.begin(), cli_vec.end(), req_leave->req_channel), cli_vec.end());

          cout << "User " << client_map[c_port].username << " has left channel " << req_leave->req_channel << endl;

          break;

        case REQ_SAY:
          req_say = (struct request_say*) &buffer;

          c_port = ntohs(src_addr.sin_port);
          ch_vec = channel_map[req_say->req_channel];

          strcpy(txt_say.txt_channel, req_say->req_channel);
          strcpy(txt_say.txt_username, client_map[c_port].username.c_str());
          strcpy(txt_say.txt_text, req_say->req_text);

          for (i = 0; i < ch_vec.size(); i++) {
            tmp_addr = client_map[ch_vec[i]].addr;
            sendto(s_socket, (void*) &txt_say, sizeof(txt_say), 0, (struct sockaddr*) &tmp_addr, sizeof(tmp_addr));
          }

          break;

        case REQ_LIST:
          /*
            I could not figure out how to set the static array of channel_info within text_list.
          */


          req_list = (struct request_list*) &buffer;

          // ch_info = (struct channel_info*) malloc(sizeof(struct channel_info) * channel_map.size());

          ch_info = (struct channel_info*) malloc(sizeof(struct channel_info) * channel_map.size());
          txt_list = (struct text_list*) malloc(sizeof(struct text_list) + sizeof(*ch_info));
          memcpy(txt_list->txt_channels, ch_info, sizeof(*ch_info));

          i = 0;
          for (channel_map_it = channel_map.begin(); channel_map_it != channel_map.end(); channel_map_it++) {
            strcpy(ch_info[i].ch_channel, channel_map_it->first.c_str());
          }

          cout << "Existing channels:" << endl;

          for (i = 0; i < channel_map.size(); i++) {
            cout << txt_list->txt_channels[i].ch_channel << endl;
          }

          // txt_list = (struct text_list*) malloc(sizeof(struct text_list) + sizeof(ch_info));
          // txt_list->txt_type = TXT_LIST;
          // txt_list->txt_nchannels = channel_map.size();
          // txt_list->txt_channels = ch_info;


          break;

        case REQ_WHO:

          break;

        default:
          cout << "Unknown request" << endl;
      }
    }
  }

  return 0;
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
