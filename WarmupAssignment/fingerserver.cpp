/* fingerserver using TCP connection for multiple client connections
* IP Address: cs1.seattleu.edu, Port Number: 10042 */
// 5th on roster: using Port# 10040-10049
// TODO: test on cs2.seattleu.edu

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <pthread.h>
#include <iostream>
#include <unistd.h>

#define PORT_NUMBER "10042"
#define MAXDATASIZE 100   // max size of client's username
#define CONNECTIONS_ALLOWED 10

int main(int argc, char * argv[]) {

  int sockfd, new_sockfd;
  struct addrinfo hints, *servinfo, *p;
  struct sockaddr_storage client_addr;
  socklen_t addr_size;
  int rv;
  pid_t cp_id;  // child process id

  int numbytes;
  char username_buf[MAXDATASIZE];

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;  // use AF_INET6 to force IPv6
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;  // uses current IP address
                                // (clients expect cs1.seattleu.edu)

  if((rv = getaddrinfo(NULL, PORT_NUMBER, &hints, &servinfo)) != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
    exit(1);
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
    exit(1);
  }

  if(listen(sockfd, CONNECTIONS_ALLOWED) == -1) {
    perror("listen");
    exit(1);
  };

  // accept incomming connections
  while(true) {
    addr_size = sizeof client_addr;
    new_sockfd = accept(sockfd, (struct sockaddr *)&client_addr, &addr_size);
    if(new_sockfd == -1) {
      perror("accept");
      continue;
    }

    cp_id = fork();
    if(cp_id == -1) {
      perror("fork");
      exit(1);
    }

    if(cp_id == 0) {  // child process executing
      close(sockfd);  // child shouldn't have a listener

      if((numbytes = recv(new_sockfd, username_buf, MAXDATASIZE-1, 0)) == -1) {
        perror("recv");
        exit(1);
      }
      username_buf[numbytes] = '\0';
      printf("Message from client: '%s'\n", username_buf);

      if((dup2(new_sockfd, 1))!= 1 || (dup2(new_sockfd, 2)) != 2) {
        perror("dup2");
      }
      dup2(new_sockfd, 1);
      dup2(new_sockfd, 2);

      if((execl("/usr/bin/finger", "finger", username_buf, NULL)) == -1) {
        perror("execl");
      };

      close(new_sockfd);
      exit(0);
    }
    else {  // parent process executing
      close(new_sockfd);  // parent shouldn't keep child sockets
    }
  }

  return 0;
}
