#ifndef ROBOSERVELIB__H
#define ROBOSERVELIB__H

#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>

struct message;

struct server
{
	int sockfd, newsockfd, portno;

	struct message * pages;

	struct sockaddr_in serv_addr;

	pthread_t listen_thread;

	char serve_dir[128];
};

struct server * new_server();

void free_server(struct server * server);

struct message *add_message(struct server *server, char * name);

void add_float_box(struct message *message, char *name, float * variable);

void add_int_box(struct message *message, char *name, int * variable);

void add_string_box(struct message *message, char *name, char * variable);

int start_server(struct server * server);

#endif
