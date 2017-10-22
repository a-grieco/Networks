/* Madeline Wong, Matthew Irwin, Adrienne Grieco
 * Phase 1: Implementing a Simple HTTP Web Proxy
 * 10/29/2017
 * client.cpp */

// client connects to proxy to retrieve data from a given web server

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

#define BUFFERSIZE 5000   // for reading response from server

bool parse_user_arg(std::string& username, std::string& hostname,
  std::string& server_port, std::string user_arg);
bool arg_format_is_valid(std::size_t at_pos, std::size_t colon_pos,
  std::string user_arg);
void connect_to_server(std::string hostname, std::string server_port,
  int& sockfd);
int send_all(int socket, char *data_buf, int *length);
void print_message_from_server(int sockfd);

int main(int argc, char *argv[]) {

  std::string username, hostname, server_port;

  // args s/b 3 or greater
  // parse request header <METHOD> <URL> <HTTP VERSION>
  // and optional/indefinite number of <HEADER NAME>: <HEADER VALUE> args
  if(argc < 2 || parse_user_arg(username, hostname, server_port,
      std::string(argv[1])) == false) {
    fprintf(stderr, "usage: %s username@hostname:server_port\n", argv[0]);
    exit(EXIT_FAILURE);
  }

  int sockfd;
  connect_to_server(hostname, server_port, sockfd);

  // send loop using beej's "sendall" function template
  int length = username.length();
  if(send_all(sockfd, (char*)username.c_str(), &length) == -1) {
    perror("send");
    printf("send_all only successfully sent %d bytes.\n", length);
    exit(EXIT_FAILURE);
  }

  print_message_from_server(sockfd);

  // close the connection with the server
  close(sockfd);

  return 0;
}

/* Returns true and parses the user argument by assigning values to username,
 * hostname, and server_port if the argument is correctly formatted; otherwise
 * returns false */
bool parse_user_arg(std::string& username, std::string& hostname,
  std::string& server_port, std::string user_arg) {
    bool is_valid = false;
    std::size_t at_pos, colon_pos;
    at_pos = user_arg.find("@");
    colon_pos = user_arg.find(":");
    if(arg_format_is_valid(at_pos, colon_pos, user_arg)) {
      username = user_arg.substr(0, at_pos);
      hostname = user_arg.substr(at_pos + 1, colon_pos - (at_pos + 1));
      server_port = user_arg.substr(colon_pos + 1);
      is_valid = true;
    }
    return is_valid;
}

/* Checks for valid format (username@hostname:server_port): so the '@' and ':'
 * symbols are present in the expected order and each item (username, hostname,
 * and server_port) are at least 1 character in length */
bool arg_format_is_valid(std::size_t at_pos, std::size_t colon_pos,
   std::string user_arg) {
    return (at_pos != std::string::npos && colon_pos != std::string::npos &&
        at_pos > 0 && colon_pos > (at_pos + 1) &&
        user_arg.length() > (colon_pos + 1));
}

/* Establish a connection with the server at the given hostname and port number
 * or print error and exit on failure. */
void connect_to_server(std::string hostname, std::string server_port,
    int& sockfd) {
  struct addrinfo hints, *servinfo, *p;
  int rv;

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;  // use AF_INET6 to force IPv6
  hints.ai_socktype = SOCK_STREAM;

  if((rv = getaddrinfo(hostname.c_str(), server_port.c_str(), &hints,
      &servinfo)) != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
    exit(EXIT_FAILURE);
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

  if(p == NULL) {   // p reached NULL with no connection
    fprintf(stderr, "failed to connect\n");
    exit(EXIT_FAILURE);
  }
}

/* handles partial sends - using sample from beej */
int send_all(int socket, char *data_buf, int *length) {
  int total_bytes_sent = 0;
  int bytes_left = *length;
  int numbytes;

  while(total_bytes_sent < *length) {
    numbytes = send(socket, data_buf+total_bytes_sent, bytes_left, 0);
    if(numbytes ==  -1) { break; }
    total_bytes_sent += numbytes;
    bytes_left -= numbytes;
  }

  *length = total_bytes_sent; // assign actual number of bytes sent
  return numbytes == -1 ? -1 : 0; // return -1 on success or 0 on failure
}

/* Reads and prints a message received from the server */
void print_message_from_server(int sockfd) {
  int numbytes;
  char mssg_buf[BUFFERSIZE];
  bool message_completed = false;
  while(!message_completed) {
    if((numbytes = recv(sockfd, mssg_buf, BUFFERSIZE-1, 0)) == -1) {
      perror("recv");
      exit(EXIT_FAILURE);
    }
    if(numbytes == 0) {
      message_completed = true;
      printf("\n");
    }
    else {
      mssg_buf[numbytes] = '\0';
      printf("%s", mssg_buf);
    }
  }
}
