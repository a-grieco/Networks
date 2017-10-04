/* fingerserver using TCP connection for multiple fingerclient connections
* IP Address: cs1.seattleu.edu, Port Number: 10042
* (5th on roster: using Port Numbers 10040-10049) */
// TODO: test on cs2.seattleu.edu

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <iostream>
#include <signal.h>

#define PORT_NUMBER "10042"
#define MAXDATASIZE 100         // max size of client's username
#define CONNECTIONS_ALLOWED 10  // max number of clients serviceable

bool display_status = true;

void create_and_bind_to_socket(int& sockfd);
void send_response_to_client(int new_sockfd);
void clean_exit(int flag);

int main(int argc, char * argv[]) {

  int sockfd, new_sockfd;
  struct sockaddr_storage client_addr;
  socklen_t addr_size;
  pid_t pid;  // child process id

  create_and_bind_to_socket(sockfd);

  // listen for incomming client connections
  if(listen(sockfd, CONNECTIONS_ALLOWED) == -1) {
    perror("listen");
    exit(EXIT_FAILURE);
  };

  if(display_status) {
    printf("Server listening for connections...\n");
  }

  // accept incomming connections
  while(true) {
    addr_size = sizeof client_addr;
    new_sockfd = accept(sockfd, (struct sockaddr *)&client_addr, &addr_size);
    if(new_sockfd == -1) {
      perror("accept");
      continue;
    }

    switch(pid = fork()) {
    // error creating child process
    case -1:
      perror("fork");
      exit(EXIT_FAILURE); // parent exits
    // child process executing
    case 0 :
      close(sockfd);  // child shouldn't have a listener
      send_response_to_client(new_sockfd);
    // parent process executing
    default:
      close(new_sockfd);  // parent shouldn't keep child sockets
    }
  }

  signal(SIGTERM, clean_exit);
  signal(SIGINT, clean_exit);

  return 0;
}

/* Create a socket and bind to it based on the defined port number or print
 * error and exit on failure. */
void create_and_bind_to_socket(int& sockfd) {
  struct addrinfo hints, *servinfo, *p;
  int rv;

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;  // use AF_INET6 to force IPv6
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;  // uses current IP address
                                // (clients expect cs1.seattleu.edu)

  if((rv = getaddrinfo(NULL, PORT_NUMBER, &hints, &servinfo)) != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
    exit(EXIT_FAILURE);
  }

  // loop through all the results and connect to the first one possible
  for(p = servinfo; p != NULL; p = p->ai_next) {
    if((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
      perror("socket");
      continue;
    }
    if(bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
      perror("bind");
      close(sockfd);
      continue;
    }
    break;  // if code reaches this point, connection was made successfully
  }

  freeaddrinfo(servinfo); // free memory

  // if p reached NULL with no bind, print error and exit
  if(p == NULL) {
    fprintf(stderr, "failed to bind socket\n");
    exit(EXIT_FAILURE);
  }
}

/* Helps to ensure port is freed upoon a "rough" exit from a program */
void clean_exit(int flag) {
  exit(EXIT_SUCCESS);
}

/* Executes child process of sending finger service data to fingerclient;
 * ensures child socket is closed upon completion. */
void send_response_to_client(int new_sockfd) {
  int numbytes;
  char username_buf[MAXDATASIZE];

  if((numbytes = recv(new_sockfd, username_buf, MAXDATASIZE-1, 0)) == -1) {
    perror("recv");
    exit(EXIT_FAILURE);
  }
  username_buf[numbytes] = '\0';

  if(display_status) {
    printf("   Username received: '%s'\n", username_buf);
  }

  if((dup2(new_sockfd, 1))!= 1 || (dup2(new_sockfd, 2)) != 2) {
    perror("dup2");
  }
  close(new_sockfd);

  if((execl("/usr/bin/finger", "finger", username_buf, NULL)) == -1) {
    perror("execl");
  };

  exit(EXIT_FAILURE); // code should not reach this point using execl()
                      // in other cases, reaching here indicates success
}
