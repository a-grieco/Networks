/* Adrienne Grieco
 * Simple Client/Server Warmup Project
 * 10/05/2017
 * fingerserver.cpp */

 /* fingerserver using TCP connection for multiple fingerclient connections */
 // IP Address: cs1 or cs2.seattleu.edu, Port Number: 10042 (default)
 // (5th on roster: using Port Numbers 10040-10049)

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <netdb.h>
#include <iostream>
#include <signal.h>
#include <errno.h>

#define DISPLAY_STATUS false    // prints server status if set to true

#define DEFAULT_PORT_NUMBER 10042
#define MAXDATASIZE 100         // max size of client's username
#define CONNECTIONS_ALLOWED 10  // max number of clients serviceable

bool port_number_is_valid(int& port_int, int port_number_arg);
void set_port_number(char* port_buf, int port_int);
void create_and_bind_to_socket(int& sockfd, const char* port_buf);
void send_response_to_client(int new_sockfd);
void clean_exit(int flag);
void reap_zombie_processes(struct sigaction& sa);
void sigchild_handler(int s);

int main(int argc, char * argv[]) {

  int port_int = DEFAULT_PORT_NUMBER; // default used without user argument
  char port_buf[5];

  if(argc > 2) {
    fprintf(stderr, "usage: %s or %s server_port\n", argv[0]);
    exit(EXIT_FAILURE);
  }

  // reassign port number if user includes valid argument
  if(argc == 2 && !port_number_is_valid(port_int, atoi(argv[1]))) {
    fprintf(stderr, "usage: %s server_port (10000-13000 allowable)\n", argv[0]);
    exit(EXIT_FAILURE);
  }
  set_port_number(port_buf, port_int);

  int sockfd, new_sockfd;
  struct sockaddr_storage client_addr;
  struct sigaction sa;
  socklen_t addr_size;
  pid_t pid;

  create_and_bind_to_socket(sockfd, port_buf);

  // listen for incomming client connections
  if(listen(sockfd, CONNECTIONS_ALLOWED) == -1) {
    perror("listen");
    exit(EXIT_FAILURE);
  };
  printf("Server listening for connections...\n");

  reap_zombie_processes(sa);

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

/* Assign user specified port number and return true if argument is valid, i.e.
 * the number is between 10000 and 13000 (inclusive); otherwise return false. */
 bool port_number_is_valid(int& port_int, int port_number_arg) {
   if(port_number_arg >= 10000 && port_number_arg <=13000) {
     port_int = port_number_arg;
     return true;
   }
   return false;
 }

/* Convert port number into a usable format for socket connection */
 void set_port_number(char* port_buf, int port_int) {
   int n = sprintf(port_buf, "%d", port_int);
   if(n < 5) {
     fprintf(stderr, "failed to set port number: %d\n", port_int);
     exit(EXIT_FAILURE);
   }
   if(DISPLAY_STATUS) {
     printf("Server connecting to port number: %s\n", port_buf);
   }
 }

/* Create a socket and bind to it based on the defined port number or print
 * error and exit on failure. */
void create_and_bind_to_socket(int& sockfd, const char* port_buf) {
  struct addrinfo hints, *servinfo, *p;
  int rv;

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;  // use AF_INET6 to force IPv6
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;  // uses current IP address
                                // (clients expect cs1.seattleu.edu)

  if((rv = getaddrinfo(NULL, port_buf, &hints, &servinfo)) != 0) {
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

  if(DISPLAY_STATUS) {
    printf("   Username received: '%s'\n", username_buf);
  }

  if((dup2(new_sockfd, 1))!= 1 || (dup2(new_sockfd, 2)) != 2) {
    perror("dup2");
    exit(EXIT_FAILURE);
  }
  close(new_sockfd);

  if((execl("/usr/bin/finger", "finger", username_buf, NULL)) == -1) {
    perror("execl");
    exit(EXIT_FAILURE);
  };

  exit(EXIT_FAILURE); // code should not reach this point using execl()
                      // in other cases, reaching here indicates success
}

/* Helps to ensure port is freed upoon a "rough" exit from a program */
void clean_exit(int flag) {
  exit(EXIT_SUCCESS);
}

/* Reaps terminated child processes after they exit. */
void reap_zombie_processes(struct sigaction& sa) {
  sa.sa_handler = sigchild_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  if(sigaction(SIGCHLD, &sa, NULL) == -1) {
    perror("sigaction");
    exit(EXIT_FAILURE);
  }
}

/* Collects terminated "zombie" child processes if present, otherwise exits
 * loop. (Zombie signals are added to sa_mask of sigaction sa for removal.) */
 void sigchild_handler(int s) {
   // save and restore error number to prevent from being overwritten
   int saved_errno = errno;
   while(waitpid(-1, NULL, WNOHANG) > 0);
   errno = saved_errno;
 }
