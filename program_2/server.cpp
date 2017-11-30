#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <fcntl.h>
#include <string>
#include <map>
#include <vector>
#include <iostream>
#include <fstream>
#include <time.h>
#include <signal.h>
#include <algorithm>

using namespace std;



//#include "hash.h"
#include "duckchat.h"


#define MAX_CONNECTIONS 10
#define HOSTNAME_MAX 100
#define MAX_MESSAGE_LEN 65536

struct channel_members {
	map<string,struct sockaddr_in> channel_users; //<username, sockaddr_in of user>
	map<string, struct sockaddr_in> channel_servers; //<server, sockaddr_in of server>
};

//typedef map<string,string> channel_users; //<username, ip+port in string>
// map<string,struct sockaddr_in> channel_users; //<username, sockaddr_in of user>
// map<string, struct sockaddr_in> channel_servers; //<server, sockaddr_in of server>

int s; //socket for listening
struct sockaddr_in server;
string server_identifier;

map<string,struct sockaddr_in> usernames; //<username, sockaddr_in of user>
map<string,struct sockaddr_in> servers;		//<ip.port, sockaddr_in of server>
map<string,int> active_usernames; //0-inactive , 1-active
//map<struct sockaddr_in,string> rev_usernames;
map<string,string> rev_usernames; //<ip+port in string, username>
map<string,struct channel_members> channels;
map<string,int> active_servers; //<ip.port, 0-inactive 1-active>

vector<unsigned long long> received_uids;


void handle_socket_input();
void handle_login_message(void *data, struct sockaddr_in sock);
void handle_logout_message(struct sockaddr_in sock);
void handle_join_message(void *data, struct sockaddr_in sock);
void handle_leave_message(void *data, struct sockaddr_in sock);
void handle_say_message(void *data, struct sockaddr_in sock);
void handle_list_message(struct sockaddr_in sock);
void handle_who_message(void *data, struct sockaddr_in sock);
void handle_keep_alive_message(struct sockaddr_in sock);
void send_error_message(struct sockaddr_in sock, string error_msg);

// server-to-server messages
void handle_ss_join_message(void *data, struct sockaddr_in sock);
void handle_ss_leave_message(void *data, struct sockaddr_in sock);
void handle_ss_say_message(void *data, struct sockaddr_in sock);

void send_ss_join(string send_id, struct sockaddr_in sock, string channel);
void server_leave_channel(string key, string channel);

// soft-state functions
void handle_timer();
void renew_subscriptions();
void check_subscription_states();
void remove_server_from_channels(string key);


int timer_delay = 60; // 1 minute delay
int timer_flag;
int is_second_timer;
void on_alarm(int signum);


int main(int argc, char *argv[])
{

	if (((argc + 1) % 2) != 0 || argc < 3) // should be odd number of arguments > 3
	{
		printf("Usage: ./server domain_name port_num adj_domain1 adj_port1 adj_domain2 adj_port2 ...\n");
		exit(1);
	}

	char hostname[HOSTNAME_MAX];
	int port;

	strcpy(hostname, argv[1]);
	port = atoi(argv[2]);



	s = socket(PF_INET, SOCK_DGRAM, 0);
	if (s < 0)
	{
		perror ("socket() failed\n");
		exit(1);
	}

	//struct sockaddr_in server;

	struct hostent     *he;

	server.sin_family = AF_INET;
	server.sin_port = htons(port);

	if ((he = gethostbyname(hostname)) == NULL) {
		puts("error resolving hostname..");
		exit(1);
	}
	memcpy(&server.sin_addr, he->h_addr_list[0], he->h_length);

	string ip(inet_ntoa(server.sin_addr));
	server_identifier = ip + "." + to_string(server.sin_port);

	string server_id;
	struct sockaddr_in temp;
	// Set up tree for adjacent servers
	for (int i = 3; i < argc; i+=2) {
		strcpy(hostname, argv[i]);
		port = atoi(argv[i+1]);

		if ((he = gethostbyname(hostname)) == NULL) {
			puts("error resolving hostname..");
			exit(1);
		}

		temp.sin_family = AF_INET;
		temp.sin_port = htons(port);

		memcpy(&temp.sin_addr, he->h_addr_list[0], he->h_length);

		ip = inet_ntoa(temp.sin_addr);
		server_id = ip + "." + to_string(temp.sin_port);

		servers[server_id] = temp;
		active_servers[server_id] = 0;
	}


	int err;

	err = bind(s, (struct sockaddr*)&server, sizeof server);

	if (err < 0)
	{
		perror("bind failed\n");
	}
	else
	{
		//printf("bound socket\n");
	}





	//testing maps end

	//create default channel Common
	// string default_channel = "Common";
	// map<string,struct sockaddr_in> default_channel_users;
	// channels[default_channel].channel_users = default_channel_users;

	// start timer to go off every minute
	struct sigaction act;
	struct itimerval timer;

	memset (&act, 0, sizeof (act));
	act.sa_handler = &on_alarm;
	sigaction(SIGALRM, &act, NULL);

	timer.it_value.tv_sec = timer_delay;
	timer.it_value.tv_usec = 0;
	timer.it_interval.tv_sec = timer_delay;
	timer.it_interval.tv_usec = 0;

	setitimer (ITIMER_REAL, &timer, NULL);
	is_second_timer = 0;
	timer_flag = 0;

	while(1) //server runs for ever
	{

		// check for timer interval
		if (timer_flag == 1) {
			handle_timer();
			timer_flag = 0;
		}

		handle_socket_input();


	}



	return 0;

}

void handle_socket_input()
{

	struct sockaddr_in recv_client;
	ssize_t bytes;
	void *data;
	size_t len;
	socklen_t fromlen;
	fromlen = sizeof(recv_client);
	char recv_text[MAX_MESSAGE_LEN];
	data = &recv_text;
	len = sizeof recv_text;


	bytes = recvfrom(s, data, len, 0, (struct sockaddr*)&recv_client, &fromlen);


	if (bytes < 0)
	{
	}
	else
	{
		//printf("received message\n");

		struct request* request_msg;
		request_msg = (struct request*)data;

		//printf("Message type:");
		request_t message_type = request_msg->req_type;

		//printf("%d\n", message_type);

		if (message_type == REQ_LOGIN)
		{
			handle_login_message(data, recv_client); //some methods would need recv_client
		}
		else if (message_type == REQ_LOGOUT)
		{
			handle_logout_message(recv_client);
		}
		else if (message_type == REQ_JOIN)
		{
			handle_join_message(data, recv_client);
		}
		else if (message_type == REQ_LEAVE)
		{
			handle_leave_message(data, recv_client);
		}
		else if (message_type == REQ_SAY)
		{
			handle_say_message(data, recv_client);
		}
		else if (message_type == REQ_LIST)
		{
			handle_list_message(recv_client);
		}
		else if (message_type == REQ_WHO)
		{
			handle_who_message(data, recv_client);
		}
		else if (message_type == REQ_SS_JOIN)
		{
			handle_ss_join_message(data, recv_client);
		}
		else if (message_type == REQ_SS_LEAVE)
		{
			handle_ss_leave_message(data, recv_client);
		}
		else if (message_type == REQ_SS_SAY)
		{
			handle_ss_say_message(data, recv_client);
		}

		else
		{
			//send error message to client
			send_error_message(recv_client, "*Unknown command");
		}


	}


}

void handle_login_message(void *data, struct sockaddr_in sock)
{
	struct request_login* msg;
	msg = (struct request_login*)data;

	string username = msg->req_username;
	usernames[username]	= sock;
	active_usernames[username] = 1;

	//rev_usernames[sock] = username;

	//char *inet_ntoa(struct in_addr in);
	string ip = inet_ntoa(sock.sin_addr);
	//cout << "ip: " << ip <<endl;
	int port = sock.sin_port;
	//unsigned short short_port = sock.sin_port;
	//cout << "short port: " << short_port << endl;
	//cout << "port: " << port << endl;

 	char port_str[6];
 	sprintf(port_str, "%d", port);
	//cout << "port: " << port_str << endl;

	string key = ip + "." +port_str;
	//cout << "key: " << key <<endl;
	rev_usernames[key] = username;

	cout << "server: " << username << " logs in" << endl;





}

void handle_logout_message(struct sockaddr_in sock)
{

	//construct the key using sockaddr_in
	string ip = inet_ntoa(sock.sin_addr);
	//cout << "ip: " << ip <<endl;
	int port = sock.sin_port;

 	char port_str[6];
 	sprintf(port_str, "%d", port);
	//cout << "port: " << port_str << endl;

	string key = ip + "." +port_str;
	//cout << "key: " << key <<endl;

	//check whether key is in rev_usernames
	map <string,string> :: iterator iter;

	/*
    for(iter = rev_usernames.begin(); iter != rev_usernames.end(); iter++)
    {
        cout << "key: " << iter->first << " username: " << iter->second << endl;
    }
	*/




	iter = rev_usernames.find(key);
	if (iter == rev_usernames.end() )
	{
		//send an error message saying not logged in
		send_error_message(sock, "Not logged in");
	}
	else
	{
		//cout << "key " << key << " found."<<endl;
		string username = rev_usernames[key];
		rev_usernames.erase(iter);

		//remove from usernames
		map<string,struct sockaddr_in>::iterator user_iter;
		user_iter = usernames.find(username);
		usernames.erase(user_iter);

		//remove from all the channels if found
		map<string,struct channel_members>::iterator channel_iter;
		for(channel_iter = channels.begin(); channel_iter != channels.end(); channel_iter++)
		{
			//cout << "key: " << iter->first << " username: " << iter->second << endl;
			//channel_users current_channel = channel_iter->second;
			map<string,struct sockaddr_in>::iterator within_channel_iterator;
			within_channel_iterator = channel_iter->second.channel_users.find(username);
			if (within_channel_iterator != channel_iter->second.channel_users.end())
			{
				channel_iter->second.channel_users.erase(within_channel_iterator);
			}

		}


		//remove entry from active usernames also
		//active_usernames[username] = 1;
		map<string,int>::iterator active_user_iter;
		active_user_iter = active_usernames.find(username);
		active_usernames.erase(active_user_iter);


		cout << "server: " << username << " logs out" << endl;
	}


	/*
    for(iter = rev_usernames.begin(); iter != rev_usernames.end(); iter++)
    {
        cout << "key: " << iter->first << " username: " << iter->second << endl;
    }
	*/


	//if so delete it and delete username from usernames
	//if not send an error message - later

}

void handle_join_message(void *data, struct sockaddr_in sock)
{
	cout << "handling join" << endl;
	//get message fields
	struct request_join* msg;
	msg = (struct request_join*)data;

	string channel = msg->req_channel;

	string ip = inet_ntoa(sock.sin_addr);

	int port = sock.sin_port;

 	char port_str[6];
 	sprintf(port_str, "%d", port);
	string key = ip + "." +port_str;


	//check whether key is in rev_usernames
	map <string,string> :: iterator iter;


	iter = rev_usernames.find(key);
	if (iter == rev_usernames.end() )
	{
		//ip+port not recognized - send an error message
		send_error_message(sock, "Not logged in");
	}
	else
	{
		string username = rev_usernames[key];

		map<string,struct channel_members>::iterator channel_iter;

		channel_iter = channels.find(channel);

		active_usernames[username] = 1;

		if (channel_iter == channels.end())
		{
			cout << "creating channel " << channel << endl;
			//channel not found
			map<string,struct sockaddr_in> new_channel_users;
			new_channel_users[username] = sock;
			channels[channel].channel_users = new_channel_users;
			//cout << "creating new channel and joining" << endl;
			//broadcast to adjacent servers
			for (map<string,struct sockaddr_in>::iterator iter = servers.begin(); iter != servers.end(); iter++) {
				struct sockaddr_in send_sock = iter->second;
				send_ss_join(iter->first, send_sock, channel);
			}
		}
		else
		{
			//channel already exits

			channels[channel].channel_users[username] = sock;
			//cout << "joining exisitng channel" << endl;

		}

		cout << server_identifier << " " << key << " recv Request Join " << channel << endl;


	}

	//check whether the user is in usernames
	//if yes check whether channel is in channels
	//if channel is there add user to the channel
	//if channel is not there add channel and add user to the channel


}


void handle_leave_message(void *data, struct sockaddr_in sock)
{

	//check whether the user is in usernames
	//if yes check whether channel is in channels
	//check whether the user is in the channel
	//if yes, remove user from channel
	//if not send an error message to the user


	//get message fields
	struct request_leave* msg;
	msg = (struct request_leave*)data;

	string channel = msg->req_channel;

	string ip = inet_ntoa(sock.sin_addr);

	int port = sock.sin_port;

 	char port_str[6];
 	sprintf(port_str, "%d", port);
	string key = ip + "." +port_str;

	//check whether key is in rev_usernames
	map <string,string> :: iterator iter;


	iter = rev_usernames.find(key);
	if (iter == rev_usernames.end() )
	{
		//ip+port not recognized - send an error message
		send_error_message(sock, "Not logged in");
	}
	else
	{
		string username = rev_usernames[key];

		map<string,struct channel_members>::iterator channel_iter;

		channel_iter = channels.find(channel);

		active_usernames[username] = 1;

		if (channel_iter == channels.end())
		{
			//channel not found
			send_error_message(sock, "No channel by the name " + channel);
			cout << "server: " << username << " trying to leave non-existent channel " << channel << endl;

		}
		else
		{
			//channel already exits
			//map<string,struct sockaddr_in> existing_channel_users;
			//existing_channel_users = channels[channel];
			map<string,struct sockaddr_in>::iterator channel_user_iter;
			channel_user_iter = channels[channel].channel_users.find(username);

			if (channel_user_iter == channels[channel].channel_users.end())
			{
				//user not in channel
				send_error_message(sock, "You are not in channel " + channel);
				cout << "server: " << username << " trying to leave channel " << channel  << " where he/she is not a member" << endl;
			}
			else
			{
				channels[channel].channel_users.erase(channel_user_iter);
				//existing_channel_users.erase(channel_user_iter);
				// cout << "server: " << username << " leaves channel " << channel <<endl;

				cout << server_identifier << " " << key << " recv Request Leave " << channel << endl;

				//delete channel if no more users
				// if (channels[channel].channel_users.empty() && (channel != "Common"))
				if (channels[channel].channel_users.empty())
				{
					channels.erase(channel_iter);
				}

			}


		}




	}



}




void handle_say_message(void *data, struct sockaddr_in sock)
{

	//check whether the user is in usernames
	//if yes check whether channel is in channels
	//check whether the user is in the channel
	//if yes send the message to all the members of the channel
	//if not send an error message to the user


	//get message fields
	struct request_say* msg;
	msg = (struct request_say*)data;

	string channel = msg->req_channel;
	string text = msg->req_text;


	string ip = inet_ntoa(sock.sin_addr);

	int port = sock.sin_port;

 	char port_str[6];
 	sprintf(port_str, "%d", port);
	string key = ip + "." +port_str;


	//check whether key is in rev_usernames
	map <string,string> :: iterator iter;


	iter = rev_usernames.find(key);
	if (iter == rev_usernames.end() )
	{
		//ip+port not recognized - send an error message
		send_error_message(sock, "Not logged in ");
	}
	else
	{
		string username = rev_usernames[key];

		map<string,struct channel_members>::iterator channel_iter;

		channel_iter = channels.find(channel);

		active_usernames[username] = 1;

		if (channel_iter == channels.end())
		{
			//channel not found
			send_error_message(sock, "No channel by the name " + channel);
			cout << "server: " << username << " trying to send a message to non-existent channel " << channel << endl;

		}
		else
		{
			//channel already exits
			//map<string,struct sockaddr_in> existing_channel_users;
			//existing_channel_users = channels[channel];
			map<string,struct sockaddr_in>::iterator channel_user_iter;
			channel_user_iter = channels[channel].channel_users.find(username);

			if (channel_user_iter == channels[channel].channel_users.end())
			{
				//user not in channel
				send_error_message(sock, "You are not in channel " + channel);
				cout << "server: " << username << " trying to send a message to channel " << channel  << " where he/she is not a member" << endl;
			}
			else
			{
				map<string,struct sockaddr_in> existing_channel_users;
				existing_channel_users = channels[channel].channel_users;
				for(channel_user_iter = existing_channel_users.begin(); channel_user_iter != existing_channel_users.end(); channel_user_iter++)
				{
					//cout << "key: " << iter->first << " username: " << iter->second << endl;

					ssize_t bytes;
					void *send_data;
					size_t len;

					struct text_say send_msg;
					send_msg.txt_type = TXT_SAY;

					const char* str = channel.c_str();
					strcpy(send_msg.txt_channel, str);
					str = username.c_str();
					strcpy(send_msg.txt_username, str);
					str = text.c_str();
					strcpy(send_msg.txt_text, str);
					//send_msg.txt_username, *username.c_str();
					//send_msg.txt_text,*text.c_str();
					send_data = &send_msg;

					len = sizeof send_msg;

					//cout << username <<endl;
					struct sockaddr_in send_sock = channel_user_iter->second;


					//bytes = sendto(s, send_data, len, 0, (struct sockaddr*)&send_sock, fromlen);
					bytes = sendto(s, send_data, len, 0, (struct sockaddr*)&send_sock, sizeof send_sock);

					if (bytes < 0)
					{
						perror("Message failed\n"); //error
					}
					else
					{
						//printf("Message sent\n");

					}

				}

				// cout << "server: " << username << " sends say message in " << channel <<endl;
				cout << server_identifier << " " << key << " recv Request Say " << channel << " \"" << text << "\"" << endl;

				// generate uid for say message
				unsigned long long uid = 0;
				size_t size = sizeof(uid);
				ifstream urandom("/dev/urandom", ios::in|ios::binary);
				if(urandom) {
					urandom.read(reinterpret_cast<char*>(&uid), size);
					if(!urandom) {
						cout << "failed to read random value from /dev/urandom" << endl;
					}
					urandom.close();
				} else {
					cout << "failed to open /dev/urandom for reading" << endl;
				}

				received_uids.push_back(uid);

				// send S2S say message to adjacent servers subscribed to the channel

				map<string, struct sockaddr_in> servs = channels[channel].channel_servers;
				map<string, struct sockaddr_in>::iterator server_iter;
				for (server_iter = servs.begin(); server_iter != servs.end(); server_iter++) {
					cout << "sending s2s to server " << server_iter->first << endl;
					ssize_t bytes;
					void *send_data;
					size_t len;

					struct request_ss_say send_msg;
					send_msg.req_type = REQ_SS_SAY;

					const char* str = channel.c_str();
					strcpy(send_msg.req_channel, str);
					str = username.c_str();
					strcpy(send_msg.req_username, str);
					str = text.c_str();
					strcpy(send_msg.req_text, str);
					send_msg.req_uid = uid;

					send_data = &send_msg;

					len = sizeof send_msg;

					//cout << username <<endl;
					struct sockaddr_in send_sock = server_iter->second;


					//bytes = sendto(s, send_data, len, 0, (struct sockaddr*)&send_sock, fromlen);
					bytes = sendto(s, send_data, len, 0, (struct sockaddr*)&send_sock, sizeof send_sock);

					if (bytes < 0)
					{
						perror("Message failed\n"); //error
					}
				}
			}


		}




	}



}


void handle_list_message(struct sockaddr_in sock)
{

	//check whether the user is in usernames
	//if yes, send a list of channels
	//if not send an error message to the user



	string ip = inet_ntoa(sock.sin_addr);

	int port = sock.sin_port;

 	char port_str[6];
 	sprintf(port_str, "%d", port);
	string key = ip + "." +port_str;


	//check whether key is in rev_usernames
	map <string,string> :: iterator iter;


	iter = rev_usernames.find(key);
	if (iter == rev_usernames.end() )
	{
		//ip+port not recognized - send an error message
		send_error_message(sock, "Not logged in ");
	}
	else
	{
		string username = rev_usernames[key];
		int size = channels.size();
		//cout << "size: " << size << endl;

		active_usernames[username] = 1;

		ssize_t bytes;
		void *send_data;
		size_t len;


		//struct text_list temp;
		struct text_list *send_msg = (struct text_list*)malloc(sizeof (struct text_list) + (size * sizeof(struct channel_info)));


		send_msg->txt_type = TXT_LIST;

		send_msg->txt_nchannels = size;


		map<string,struct channel_members>::iterator channel_iter;



		//struct channel_info current_channels[size];
		//send_msg.txt_channels = new struct channel_info[size];
		int pos = 0;

		for(channel_iter = channels.begin(); channel_iter != channels.end(); channel_iter++)
		{
			string current_channel = channel_iter->first;
			const char* str = current_channel.c_str();
			//strcpy(current_channels[pos].ch_channel, str);
			//cout << "channel " << str <<endl;
			strcpy(((send_msg->txt_channels)+pos)->ch_channel, str);
			//strcpy(((send_msg->txt_channels)+pos)->ch_channel, "hello");
			//cout << ((send_msg->txt_channels)+pos)->ch_channel << endl;

			pos++;

		}



		//send_msg.txt_channels =
		//send_msg.txt_channels = current_channels;
		send_data = send_msg;
		len = sizeof (struct text_list) + (size * sizeof(struct channel_info));

					//cout << username <<endl;
		struct sockaddr_in send_sock = sock;


		//bytes = sendto(s, send_data, len, 0, (struct sockaddr*)&send_sock, fromlen);
		bytes = sendto(s, send_data, len, 0, (struct sockaddr*)&send_sock, sizeof send_sock);

		if (bytes < 0)
		{
			perror("Message failed\n"); //error
		}
		else
		{
			//printf("Message sent\n");

		}

		cout << "server: " << username << " lists channels"<<endl;


	}



}


void handle_who_message(void *data, struct sockaddr_in sock)
{


	//check whether the user is in usernames
	//if yes check whether channel is in channels
	//if yes, send user list in the channel
	//if not send an error message to the user


	//get message fields
	struct request_who* msg;
	msg = (struct request_who*)data;

	string channel = msg->req_channel;

	string ip = inet_ntoa(sock.sin_addr);

	int port = sock.sin_port;

 	char port_str[6];
 	sprintf(port_str, "%d", port);
	string key = ip + "." +port_str;


	//check whether key is in rev_usernames
	map <string,string> :: iterator iter;


	iter = rev_usernames.find(key);
	if (iter == rev_usernames.end() )
	{
		//ip+port not recognized - send an error message
		send_error_message(sock, "Not logged in ");
	}
	else
	{
		string username = rev_usernames[key];

		active_usernames[username] = 1;

		map<string,struct channel_members>::iterator channel_iter;

		channel_iter = channels.find(channel);

		if (channel_iter == channels.end())
		{
			//channel not found
			send_error_message(sock, "No channel by the name " + channel);
			cout << "server: " << username << " trying to list users in non-existing channel " << channel << endl;

		}
		else
		{
			//channel exits
			map<string,struct sockaddr_in> existing_channel_users;
			existing_channel_users = channels[channel].channel_users;
			int size = existing_channel_users.size();

			ssize_t bytes;
			void *send_data;
			size_t len;


			//struct text_list temp;
			struct text_who *send_msg = (struct text_who*)malloc(sizeof (struct text_who) + (size * sizeof(struct user_info)));


			send_msg->txt_type = TXT_WHO;

			send_msg->txt_nusernames = size;

			const char* str = channel.c_str();

			strcpy(send_msg->txt_channel, str);



			map<string,struct sockaddr_in>::iterator channel_user_iter;

			int pos = 0;

			for(channel_user_iter = existing_channel_users.begin(); channel_user_iter != existing_channel_users.end(); channel_user_iter++)
			{
				string username = channel_user_iter->first;

				str = username.c_str();

				strcpy(((send_msg->txt_users)+pos)->us_username, str);


				pos++;



			}

			send_data = send_msg;
			len = sizeof(struct text_who) + (size * sizeof(struct user_info));

						//cout << username <<endl;
			struct sockaddr_in send_sock = sock;


			//bytes = sendto(s, send_data, len, 0, (struct sockaddr*)&send_sock, fromlen);
			bytes = sendto(s, send_data, len, 0, (struct sockaddr*)&send_sock, sizeof send_sock);

			if (bytes < 0)
			{
				perror("Message failed\n"); //error
			}
			else
			{
				//printf("Message sent\n");

			}

			cout << "server: " << username << " lists users in channnel "<< channel << endl;




			}




	}




}



void send_error_message(struct sockaddr_in sock, string error_msg)
{
	ssize_t bytes;
	void *send_data;
	size_t len;

	struct text_error send_msg;
	send_msg.txt_type = TXT_ERROR;

	const char* str = error_msg.c_str();
	strcpy(send_msg.txt_error, str);

	send_data = &send_msg;

	len = sizeof send_msg;


	struct sockaddr_in send_sock = sock;



	bytes = sendto(s, send_data, len, 0, (struct sockaddr*)&send_sock, sizeof send_sock);

	if (bytes < 0)
	{
		perror("Message failed\n"); //error
	}
	else
	{
		//printf("Message sent\n");

	}





}

// Server to server messages

void handle_ss_join_message(void *data, struct sockaddr_in sock)
{
	struct request_ss_join* msg;
	msg = (struct request_ss_join*)data;

	string channel = msg->req_channel;

	string ip = inet_ntoa(sock.sin_addr);
	int port = sock.sin_port;

	string key = ip + "." + to_string(port);
	cout << server_identifier << " " << key << " recv S2S Join " << channel << endl;
	active_servers[key] = 1;

	// add sender server to list of servers for channel
	map<string, struct channel_members>::iterator channel_iter;

	channel_iter = channels.find(channel);

	if (channel_iter == channels.end()) {
		// channel not found
		map<string, struct sockaddr_in> new_channel_servers;
		new_channel_servers[key] = sock;
		channels[channel].channel_servers = new_channel_servers;

		//broadcast to adjacent servers
		for (map<string,struct sockaddr_in>::iterator iter = servers.begin(); iter != servers.end(); iter++) {
			struct sockaddr_in send_sock = iter->second;
			send_ss_join(iter->first, send_sock, channel);
		}
	} else {
		// channel exists, add server to list of channel servers
		channels[channel].channel_servers[key] = sock;
	}

	// Check if this server is subscribed to channel
	// if (find(subscribed_channels.begin(), subscribed_channels.end(), channel) == subscribed_channels.end()) {
	// 	// this server is not subscribed to the channel
	// 	struct sockaddr_in send_sock;
	// 	subscribed_channels.push_back(channel);
  //
	// 	// need to send s2s join to adjacent servers
	// 	for (map<string,struct sockaddr_in>::iterator iter = servers.begin(); iter != servers.end(); iter++) {
	// 		send_sock = iter->second;
	// 		send_ss_join(iter->first, send_sock, channel);
	// 	}
	// }
}

void handle_ss_leave_message(void *data, struct sockaddr_in sock)
{
	struct request_ss_leave* msg;
	msg = (struct request_ss_leave*)data;

	string channel = msg->req_channel;

	string ip = inet_ntoa(sock.sin_addr);

	int port = sock.sin_port;

	string key = ip + "." + to_string(port);

	server_leave_channel(key, channel);
	// cout << server_identifier << " " << key << " recv Request S2S Leave " << channel << endl;
  //
	// map<string,struct channel_members>::iterator channel_iter;
	// channel_iter = channels.find(channel);
  //
	// if (channel_iter != channels.end()) {
	// 	// iterator for map from server id to socket
	// 	map<string, struct sockaddr_in>::iterator it = channel_iter->second.channel_servers.find(key);
	// 	if (it != channel_iter->second.channel_servers.end()) {
	// 		// server id was found in list of servers for channel
	// 		channel_iter->second.channel_servers.erase(it);
	// 	} else {
	// 		cout << "server id not found in channel " << channel << endl;
	// 	}
	// } else {
	// 	cout << "channel " << channel << " not found" << endl;
	// }
}

void handle_ss_say_message(void *data, struct sockaddr_in sock)
{
	struct request_ss_say* msg;
	msg = (struct request_ss_say*)data;

	string ip = inet_ntoa(sock.sin_addr);

	int port = sock.sin_port;

 	char port_str[6];
 	sprintf(port_str, "%d", port);
	string key = ip + "." +port_str;

	string channel = msg->req_channel;
	string username = msg->req_username;
	string text = msg->req_text;

	unsigned long long uid = (unsigned long long int) msg->req_uid;

	// check if unique id of say has been received before
	if (find(received_uids.begin(), received_uids.end(), uid) == received_uids.end()) {
		// say message has not been seen before
		// send to clients and servers subscribed to channel
		received_uids.push_back(uid);

		map<string,struct sockaddr_in>::iterator channel_user_iter;
		map<string,struct sockaddr_in> existing_channel_users;
		existing_channel_users = channels[channel].channel_users;

		cout << server_identifier << " " << key << " recv Request S2S Say " << channel << " \"" << text << "\"" << endl;

		// adjacent servers on the channel
		map<string, struct sockaddr_in> servs = channels[channel].channel_servers;
		map<string, struct sockaddr_in>::iterator server_iter;

		// make sure this server has somewhere to forward the say message
		// in other words, number of users on channel > 0 or number of servers on channel > 1
		if (existing_channel_users.size() > 0 || servs.size() > 1) {
			for(channel_user_iter = existing_channel_users.begin(); channel_user_iter != existing_channel_users.end(); channel_user_iter++)
			{
				//cout << "key: " << iter->first << " username: " << iter->second << endl;

				ssize_t bytes;
				void *send_data;
				size_t len;

				struct text_say send_msg;
				send_msg.txt_type = TXT_SAY;

				const char* str = channel.c_str();
				strcpy(send_msg.txt_channel, str);
				str = username.c_str();
				strcpy(send_msg.txt_username, str);
				str = text.c_str();
				strcpy(send_msg.txt_text, str);
				//send_msg.txt_username, *username.c_str();
				//send_msg.txt_text,*text.c_str();
				send_data = &send_msg;

				len = sizeof send_msg;

				//cout << username <<endl;
				struct sockaddr_in send_sock = channel_user_iter->second;


				//bytes = sendto(s, send_data, len, 0, (struct sockaddr*)&send_sock, fromlen);
				bytes = sendto(s, send_data, len, 0, (struct sockaddr*)&send_sock, sizeof send_sock);

				if (bytes < 0)
				{
					perror("Message failed\n"); //error
				}
				else
				{
					//printf("Message sent\n");

				}

			}


			for (server_iter = servs.begin(); server_iter != servs.end(); server_iter++) {
				// send s2s message to all adjacent servers except the one that sent it
				if (server_iter->first != key) {
					ssize_t bytes;
					void *send_data;
					size_t len;

					struct request_ss_say send_msg;
					send_msg.req_type = REQ_SS_SAY;

					const char* str = channel.c_str();
					strcpy(send_msg.req_channel, str);
					str = username.c_str();
					strcpy(send_msg.req_username, str);
					str = text.c_str();
					strcpy(send_msg.req_text, str);
					send_msg.req_uid = uid;

					send_data = &send_msg;

					len = sizeof send_msg;

					//cout << username <<endl;
					struct sockaddr_in send_sock = server_iter->second;

					string send_ip = inet_ntoa(send_sock.sin_addr);

					int send_port = send_sock.sin_port;

					string send_key = send_ip + "." + to_string(send_port);

					cout << server_identifier << " " << send_key << " send S2S Say" << channel << " \"" << text << "\"" << endl;

					//bytes = sendto(s, send_data, len, 0, (struct sockaddr*)&send_sock, fromlen);
					bytes = sendto(s, send_data, len, 0, (struct sockaddr*)&send_sock, sizeof send_sock);

					if (bytes < 0)
					{
						perror("Message failed\n"); //error
					}
				}
			}
		} else {
			// nowhere to forward the say message so unsubscribe from the channel
			channels.erase(channels.find(channel));

			struct request_ss_leave send_msg;
			send_msg.req_type = REQ_SS_LEAVE;
			strcpy(send_msg.req_channel, channel.c_str());

			ssize_t bytes;
			void *send_data;
			size_t len;

			send_data = &send_msg;

			len = sizeof send_msg;

			cout << server_identifier << " " << key << " send S2S Leave " << channel << endl;

			bytes = sendto(s, send_data, len, 0, (struct sockaddr*)&sock, sizeof sock);

			if (bytes < 0)
			{
				perror("Message failed\n"); //error
			}

			// subscribed_channels.erase(std::remove(subscribed_channels.begin(), subscribed_channels.end(), channel), subscribed_channels.end());
		}

	} else {
		// send ss_leave to sender to remove loop
		struct request_ss_leave send_msg;
		send_msg.req_type = REQ_SS_LEAVE;
		strcpy(send_msg.req_channel, channel.c_str());

		ssize_t bytes;
		void *send_data;
		size_t len;

		send_data = &send_msg;

		len = sizeof send_msg;

		cout << server_identifier << " " << key << " send S2S Leave " << channel << endl;

		bytes = sendto(s, send_data, len, 0, (struct sockaddr*)&sock, sizeof sock);

		if (bytes < 0)
		{
			perror("Message failed\n"); //error
		}
	}

}

void send_ss_join(string send_id, struct sockaddr_in sock, string channel)
{
	struct request_ss_join send_msg;
	send_msg.req_type = REQ_SS_JOIN;
	const char* str = channel.c_str();
	strcpy(send_msg.req_channel, str);
	void *send_data = &send_msg;
	size_t len = sizeof send_msg;
	cout << server_identifier << " " << send_id << " send S2S Join " << channel << endl;
	ssize_t bytes = sendto(s, send_data, len, 0, (struct sockaddr*)&sock, sizeof sock);
	if (bytes < 0) {
		cout << "failed to send join message to adjacent server" << endl;
	}
}

void server_leave_channel(string key, string channel)
{
	cout << server_identifier << " " << key << " recv Request S2S Leave " << channel << endl;

	map<string,struct channel_members>::iterator channel_iter;
	channel_iter = channels.find(channel);

	if (channel_iter != channels.end()) {
		// iterator for map from server id to socket
		map<string, struct sockaddr_in>::iterator it = channel_iter->second.channel_servers.find(key);
		if (it != channel_iter->second.channel_servers.end()) {
			// server id was found in list of servers for channel
			channel_iter->second.channel_servers.erase(it);
		} else {
			cout << "server id not found in channel " << channel << endl;
		}
	} else {
		cout << "channel " << channel << " not found" << endl;
	}
}

void on_alarm(int signum)
{
	timer_flag = 1;
}

void handle_timer()
{
	renew_subscriptions();
	if (is_second_timer == 1) {
		check_subscription_states();
		is_second_timer = 0;
	} else {
		is_second_timer = 1;
	}
}

void renew_subscriptions()
{
	cout << "renewing subscriptions..." << endl;
	// create iterator for adjacent servers to send join to
	map<string, struct sockaddr_in>::iterator server_iter;

	for (server_iter = servers.begin(); server_iter != servers.end(); server_iter++) {
		// create iterator for channels this server is subscribed to
		map<string, struct channel_members>::iterator channels_iter;
		for (channels_iter = channels.begin(); channels_iter != channels.end(); channels_iter++) {
			send_ss_join(server_iter->first, server_iter->second, channels_iter->first);
		}
	}


}

void check_subscription_states()
{
	cout << "cancelling old subscriptions..." << endl;
	// iterate through active servers map, if active state is 0 then remove from all channels
	map<string, int>::iterator active_iter;
	for (active_iter = active_servers.begin(); active_iter != active_servers.end(); active_iter++) {
		// remove inactive server from channels
		if (active_iter->second == 0) {
			remove_server_from_channels(active_iter->first);
		}
		// reset active state to 0
		active_iter->second = 0;
	}
}

void remove_server_from_channels(string key)
{
	// iterate through channels
	map<string, struct channel_members>::iterator channels_iter;
	for (channels_iter = channels.begin(); channels_iter != channels.end(); channels_iter++) {
		// check if the channel contains the server key
		if (channels_iter->second.channel_servers.find(key) != channels_iter->second.channel_servers.end()) {
			cout << "removing " << key << " from channel " << channels_iter->first << endl;
			server_leave_channel(key, channels_iter->first);
		}
	}
}
