/* Madeline Wong, Matthew Irwin, Adrienne Grieco
 * Phase 1: Implementing a Simple HTTP Web Proxy
 * 10/29/2017
 * proxy.cpp */

// proxy receives data requests from client and uses HTTP to retrieve and
// forward data from a given web server back to the client

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
#include <vector>

#define DEBUG_MODE true

#define DEFAULT_PORT_NUMBER 10042
#define CONNECTIONS_ALLOWED 1  // will need to change for phtreads
#define BUFFERSIZE 5000   // for reading response from web server
#define MAXDATASIZE 100000


// this declaration section can go in the proxy.h file later...
bool port_number_is_valid(int& port_int, int port_number_arg);
void set_port_number(char* port_buf, int port_int);
void create_and_bind_to_socket(int& webserv_sockfd, const char* port_buf);
std::string get_msg_from_client(int webserv_sockfd);
bool parse_client_msg(std::string msg, std::string& host, std::string& path,
    std::string& port, std::vector<std::string> &headers);
bool parse_request_line(std::string req, std::string& host, std::string& path,
    std::string &port);
bool parse_headers(std::string msg, std::vector<std::string> &headers);
bool extract_request_elements(std::string req, std::string& method,
    std::string& url, std::string& http_vers);
bool get_next_element(std::string& src, std::string& elem);
bool verify_method(std::string& method);
void connect_to_web_server(std::string webserv_host, std::string webserv_port,
    int& webserv_sockfd);
int send_all(int socket, char *data_buf, int *length);
void print_data_from_socket(int webserv_sockfd);
std::string& trim(std::string& str);
void clean_exit(int flag);


int main(int argc, char * argv[]) {

  int port_int = DEFAULT_PORT_NUMBER; // default used without user argument
  char port_buf[5];

  if(argc > 2) {
    fprintf(stderr, "usage: %s or %s proxy_port\n", argv[0]);
    exit(EXIT_FAILURE);
  }

  // reassign port number if user includes valid argument
  if(argc == 2 && !port_number_is_valid(port_int, atoi(argv[1]))) {
    fprintf(stderr, "usage: %s proxy_port (10000-13000 allowable)\n", argv[0]);
    exit(EXIT_FAILURE);
  }
  set_port_number(port_buf, port_int);

  /* *********************** get data from client(s) *********************** */

  int client_sockfd, new_sockfd;
  struct sockaddr_storage client_addr;
  struct sigaction sa;
  socklen_t addr_size;

  create_and_bind_to_socket(client_sockfd, port_buf);

  // listen for incomming client connections
  if(listen(client_sockfd, CONNECTIONS_ALLOWED) == -1) {
    perror("listen");
    exit(EXIT_FAILURE);
  };
  printf("Proxy listening for connections...\n");

  // accept incomming connections
  while(true) {
    addr_size = sizeof client_addr;
    new_sockfd = accept(client_sockfd, (struct sockaddr *)&client_addr, &addr_size);
    if(new_sockfd == -1) {
      perror("accept");
      continue;
    }

    std::string client_msg;
    std::vector<std::string> headers;
    bool msg_is_valid;
    client_msg = get_msg_from_client(new_sockfd);
    if(DEBUG_MODE) { printf("client_msg:\n[%s]\n", client_msg.c_str()); }

    std::string webserv_host, webserv_path, webserv_port;
    msg_is_valid = parse_client_msg(client_msg, webserv_host, webserv_path,
        webserv_port, headers);

  }

  /* ********** connecting to and requesting data from webserver ********** */
  // int webserv_sockfd;
  //
  // webserv_host = "www.yahoo.com";
  // webserv_port = "80";
  //
  // std:: string data = "GET / HTTP/1.0\nwebserv_host:www.yahoo.com\nConnection:close\n\n";
  //
  // connect_to_web_server(webserv_host, webserv_port, webserv_sockfd);
  // if(DEBUG_MODE) { printf("connection to web server successful\n"); }
  // //print_data_from_socket(webserv_sockfd);
  //
  // int length = data.length();
  // if(DEBUG_MODE) { printf("attempting to send data\n"); }
  // if(send_all(webserv_sockfd, (char*)data.c_str(), &length) == -1) {
  //   perror("send");
  //   printf("send_all only successfully sent %d bytes.\n", length);
  //   exit(EXIT_FAILURE);
  // }
  // if(DEBUG_MODE) { printf("data from webserver:\n"); }
  // print_data_from_socket(webserv_sockfd);

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
   if(DEBUG_MODE) {
     printf("Proxy connecting to port number: %s\n", port_buf);
   }
 }

/* Create a socket and bind to it based on the defined port number or print
 * error and exit on failure. */
void create_and_bind_to_socket(int& webserv_sockfd, const char* port_buf) {
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
    if((webserv_sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
      perror("socket");
      continue;
    }
    if(bind(webserv_sockfd, p->ai_addr, p->ai_addrlen) == -1) {
      perror("bind");
      close(webserv_sockfd);
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

void connect_to_web_server(std::string webserv_host, std::string webserv_port,
    int& webserv_sockfd) {
  struct addrinfo hints, *servinfo, *p;
  int rv;

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;  // use AF_INET6 to force IPv6
  hints.ai_socktype = SOCK_STREAM;

  if((rv = getaddrinfo(webserv_host.c_str(), webserv_port.c_str(), &hints,
      &servinfo)) != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
    exit(EXIT_FAILURE);
  }

  // loop through all the results and connect to the first one possible
  for(p = servinfo; p != NULL; p = p->ai_next) {
    if((webserv_sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
      perror("socket");
      continue;
    }
    if(connect(webserv_sockfd, p->ai_addr, p->ai_addrlen) == -1) {
      perror("connect");
      close(webserv_sockfd);
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

/* Reads and prints a message on the given socket */
void print_data_from_socket(int webserv_sockfd) {
  int numbytes;
  char mssg_buf[BUFFERSIZE];
  bool message_completed = false;
  while(!message_completed) {
    if((numbytes = recv(webserv_sockfd, mssg_buf, BUFFERSIZE-1, 0)) == -1) {
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

/* receives HTTP message from client and returns as a string; marks the end of
 * the message with two repeating newlines (4 characters: '\r\n\r\n') */
std::string get_msg_from_client(int webserv_sockfd) {
  int numbytes, totalbytes = 0;
  char mssg_buf[BUFFERSIZE];
  char fullmssg_buf[MAXDATASIZE];
  char msg_end[] = { '\r', '\n', '\r', '\n' };
  int end_size = sizeof(msg_end);
  bool message_completed = false;
  if(DEBUG_MODE) { printf("\nRetrieving message...\n"); }
  // retrieve message from client
  while(!message_completed) {
    if((numbytes = recv(webserv_sockfd, mssg_buf, BUFFERSIZE-1, 0)) == -1) {
      perror("recv");
      exit(EXIT_FAILURE);
    }
    totalbytes += numbytes;
    strcat(fullmssg_buf,mssg_buf);
    if(DEBUG_MODE) {
      mssg_buf[numbytes] = '\0';
      printf("   %s", mssg_buf);
    }
    memset(mssg_buf, 0, sizeof mssg_buf);

    // check if client message is complete
    if(totalbytes >= end_size) {
      message_completed = true;
      for(int i = 0; i < end_size; ++i) {
        if(msg_end[i] != fullmssg_buf[totalbytes-end_size+i]) {
          message_completed = false;
          break;
        }
      }
    }
  }
  if(DEBUG_MODE) { printf("...message complete\n"); }
  return std::string(fullmssg_buf);
}

bool parse_client_msg(std::string msg, std::string& host, std::string& path,
    std::string& port, std::vector<std::string> &headers) {
  bool is_valid = true;
  printf("%s\n", "just hanging out in parse client");
  // get first line (client request) and parse/verify formatting
  std::string delimiter = "\r\n";
  std::size_t pos = msg.find(delimiter);
  std::string req = msg.substr(0, pos);
  msg.erase(0, pos + delimiter.size());
  if(DEBUG_MODE) {
    printf("request line: %s\n", req.c_str());
    printf("rest: %s\n", msg.c_str());
  }
  is_valid = parse_request_line(req, host, path, port);
  // parse/verify formatting for any header lines
  if(is_valid && msg.size() > 0) {
    is_valid = parse_headers(msg, headers);
  }
  return is_valid;
}

/* parses request line into host, path, and port number and returns true;
 * otherwise, returns false for invalid formatting and/or bad DNS */
bool parse_request_line(std::string req, std::string& host, std::string& path,
    std::string &port) {
  bool is_valid = true;
  printf("parsing request line\n");
  // line should contain 3 parts <METHOD> <URL> <HTTP VERSION>
  std::string method, url, http_vers;
  is_valid = extract_request_elements(req, method, url, http_vers);
  // if(is_valid) {
  //   if( verify_method(method); && etc...
  // }

  return is_valid;
  // TODO: check DNS for valid path
}

/* extracts three space-delimited elements from the client request line: METHOD,
 * URL, and HTTP VERSION, assigns to respective variables, and returns true;
 * otherwise, returns false if there are more/fewer than three elements.
 * Note: does not ensure validity of variables */
bool extract_request_elements(std::string req, std::string& method,
    std::string& url, std::string& http_vers) {
  printf("getting request elements\n");

  bool is_valid = true;
  trim(req);

  if(get_next_element(req, method) &&
     get_next_element(req, url) &&
     get_next_element(req, http_vers)) {
       printf("method: %s, url: %s, http_vers: %s\n", method.c_str(),
        url.c_str(), http_vers.c_str());
    return true;
  }
  return false;
}

bool get_next_element(std::string& src, std::string& elem) {
  std::string delimiter = " ";
  if(src.empty()) { return false; }
  std::size_t pos = src.find(delimiter);
  // only one element present
  if(pos == std::string::npos && !src.empty()) {
    elem = src;
    return true;
  }
  // extract first element found
  if(pos != std::string::npos){
    elem = src.substr(0, pos);
    src.erase(0, pos + delimiter.size());
    trim(src);
    return true;
  }
  return false;
}

/* verifies that method is accepted (only GET in this assignment), and assigned
 * in uppercase; otherwise returns false on mismatch */
bool verify_method(std::string& method) {
  std::string valid_method = "GET";
  if(method.length() == valid_method.length()) {
    for(int i = 0; i < valid_method.length(); ++i) {
      if(toupper(method[i]) != valid_method[i]) {
        return false;
      }
    }
    method = valid_method;
    return true;  // method = "GET"
  }
  else { return false; }
}

/* parses headers into a vector of strings (each index as <name: value>) and
 * returns true; otherwise returns false for invalid formatting */
bool parse_headers(std::string msg, std::vector<std::string> &headers) {
  printf("parsing headers...\n");
  return true;
}

/* trims leading and trailing whitespace from a string; allows correction of
 * excess whitespace in a line of the client request */
std::string& trim(std::string& str)
{
  std::string whitespace = " \r\n\t";
	str.erase(0, str.find_first_not_of(whitespace));
	str.erase(str.find_last_not_of(whitespace) + 1);
	return str;
}

/* Helps to ensure port is freed upoon a "rough" exit from a program */
void clean_exit(int flag) {
  exit(EXIT_SUCCESS);
}
