/* fingerclient using TCP connection to connect to server
 * IP Address: cs1.seattleu.edu, Port Number: 10042 */
 // 5th on roster: using Port#s 10040-10049
// TODO: test on cs2.seattleu.edu

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <string>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <iostream>
#include <limits>

#define PORT_NUMBER "10042"
#define MAXDATASIZE 100

/* Checks for valid argument format (username@hostname:server_port): so the '@'
 * and ':' symbols are present in the expected order and each item (username,
 * hostname,and server_port) are at least 1 character in length */
bool arg_format_is_valid(std::size_t at_pos, std::size_t colon_pos,
  std::string user_arg) {
    return (at_pos != std::string::npos && colon_pos != std::string::npos &&
        at_pos > 0 && colon_pos > (at_pos + 1) &&
        user_arg.length() > (colon_pos + 1));
}

int main(int argc, char *argv[]) {

  if(argc < 2) {
    fprintf(stderr, "usage %s username@hostname:server_port\n", argv[0]);
    exit(0);
  }
  // parse user argument
  std::string user_arg = std::string(argv[1]);
  std::string username, hostname, server_port;
  std::size_t at_pos, colon_pos;
  at_pos = user_arg.find("@");
  colon_pos = user_arg.find(":");
  // printf("at_pos: %d, colon_pos: %d\n", at_pos, colon_pos); DELETE

  if(arg_format_is_valid(at_pos, colon_pos, user_arg)) {
    username = user_arg.substr(0, at_pos);
    hostname = user_arg.substr(at_pos + 1, colon_pos - (at_pos + 1));
    server_port = user_arg.substr(colon_pos + 1);
  }
  else {
    fprintf(stderr, "usage %s username@hostname:server_port\n", argv[0]);
    exit(0);
  }
  printf("username: %s\nhostname: %s\nserver_port: %s\n",
    username.c_str(), hostname.c_str(), server_port.c_str());

  int sockfd;
  struct addrinfo hints, *servinfo, *p;
  int rv;

  int numbytes;
  char buf[100];

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;  // use AF_INET6 to force IPv6
  hints.ai_socktype = SOCK_STREAM;

  if((rv = getaddrinfo(hostname.c_str(), server_port.c_str(), &hints,
      &servinfo)) != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
    exit(1);
  }

  // loop through all the results and connect to the first one possible
  for(p = servinfo; p != NULL; p = p->ai_next) {
    if((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
      perror("socket");
      continue;
    }
    if(connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
      perror("connect");
      close(sockfd);
      continue;
    }
    break;  // if code reaches this point, connection was made successfully
  }

  freeaddrinfo(servinfo); // free memory

  // if p reached NULL with no connection, print error and exit
  if(p == NULL) {
    fprintf(stderr, "failed to connect\n");
    exit(2);
  }

  if(send(sockfd, username.c_str(), 13, 0) == -1) {
    perror("send");
  }

  // close the connection with the server
  close(sockfd);

  return 0;
}
