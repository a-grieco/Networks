/* Madeline Wong, Matthew Irwin, Adrienne Grieco
 * Phase 2: Implementing a Simple, Multithreaded HTTP Web Proxy
 * 11/05/2017
 * proxy.cpp */

// proxy receives data requests from client and uses HTTP to retrieve and
// forward data from a given web server back to the client; multithreaded -
// one thread per HTTP request

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
#include <errno.h>
#include <vector>
#include <sstream>
#include <iterator>
#include <sys/time.h> // using wall clock time
#include <pthread.h>
#include <thread>
#include <chrono>

#include "parse.h"

const bool DEBUG_MODE = true;
const bool INCLUDE_PROXY_ERROR_MSGS = true;

const std::string STANDARD_ERROR_MESSAGE = "HTTP/1.0 500 Internal error\n";
const std::string HTTP_BODY_HEADER = "Content-Length";  // # bytes in body

const bool PREEMPT_EXIT = true;     // if true, exits if the time to complete...
const int MAX_SECONDS_TO_WAIT = 5;  // ...exceeds MAX_SECONDS_TO_WAIT
const int STANDARD_PORT = 80;

const int MAX_BLANK_RECVS = 3;  // max consecutive 0 bytes recv() allowed

const int DEFAULT_PORT_NUMBER = 10042;

const int CONNECTIONS_ALLOWED = 30;
const int MAX_THREADS_SUPPORTED = 30;

const int MAX_SLEEP_SECONDS = 5;  // wait before attempt to create thread

const int SMALL_BUFFERSIZE = 1000;
const int BUFFERSIZE = 3000;

enum Proxy_Error { e_serv_connect, e_serv_send, e_serv_recv, e_empty_req };

int thread_count = -1;  // initial connection brings count to 0
pthread_mutex_t count_mutex = PTHREAD_MUTEX_INITIALIZER;
void decrement_thread_count();
bool increment_thread_count_successful();

void* thread_connect (void * new_sockfd_ptr);

bool port_number_is_valid(int& port_int, int port_number_arg);
void set_port_number(char* port_buf, int port_int);

void create_and_bind_to_socket(int& client_sockfd, const char* port_buf);
bool get_msg_from_client(int client_sockfd, char (&req_buf)[SMALL_BUFFERSIZE],
  char (&header_buf)[BUFFERSIZE], char (&body_buf)[BUFFERSIZE],
  size_t& num_bytes_request, size_t& num_bytes_headers, size_t& num_bytes_body);
bool connect_to_web_server(std::string webserv_host, std::string webserv_port,
    int& webserv_sockfd);
bool send_webserver_data_to_client(int webserv_sockfd, int new_sockfd);
void send_error_to_client(int& client_sockfd, std::string& parse_err);
void send_error_to_client(int& client_sockfd, Proxy_Error& err);

bool req_header_end_detected(std::string& msg, std::string& end_used,
  size_t& msg_end_found, std::string& req_end_used, size_t& req_end_found,
  bool& headers_detected);
int get_bytes_in_body(std::string& msg, size_t msg_end);
void get_proxy_error_msg(std::string msg, Proxy_Error& err);
int send_all(int socket, char *data_buf, int *length);
void clean_exit(int flag);

int main(int argc, char * argv[]) {

  int port_int = DEFAULT_PORT_NUMBER; // default used without user argument
  char port_buf[5];

  if(argc > 2) {
    fprintf(stderr, "usage: %s or %s proxy_port\n", argv[0]);
    exit(EXIT_FAILURE);
  }

  // reassign port number if client includes valid argument
  if(argc == 2 && !port_number_is_valid(port_int, atoi(argv[1]))) {
    fprintf(stderr, "usage: %s proxy_port (10000-13000 allowable)\n", argv[0]);
    exit(EXIT_FAILURE);
  }
  set_port_number(port_buf, port_int);

  int client_sockfd;
  create_and_bind_to_socket(client_sockfd, port_buf);

  // listen for incomming client connections
  if(listen(client_sockfd, CONNECTIONS_ALLOWED) == -1) {
    perror("listen");
    exit(EXIT_FAILURE);
  };
  if(DEBUG_MODE) { printf("Proxy listening for connections...\n"); }

  // initialize detached attribute (detached threads exit without joining)
  pthread_attr_t attr;
  int ret;
  if((ret = pthread_attr_init(&attr))) {
    printf("pthread_attr_init() error %d\n", ret);
    exit(EXIT_FAILURE);
  }
  if((ret = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED))) {
    printf("pthread_attr_setdetachstate() error %d\n", ret);
    exit(EXIT_FAILURE);
  }

  signal(SIGPIPE, SIG_IGN); // ignore signals to shut down proxy

  // accept incoming connections
  while(true) {
    int new_sockfd;
    struct sockaddr_storage client_addr;
    struct sigaction sa;
    socklen_t addr_size;
    addr_size = sizeof client_addr;

    // only allow MAX_THREADS_SUPPORTED to exist simultaneously
    int sleep_seconds = 0;
    while(!(increment_thread_count_successful())) {
     // wait before trying again (1 sec -> MAX_SLEEP_SECONDS)
     if(sleep_seconds < MAX_SLEEP_SECONDS) { ++sleep_seconds; }
     else { sleep_seconds = sleep_seconds / 2; }
     std::this_thread::sleep_for(std::chrono::seconds(sleep_seconds));
    }

    new_sockfd = accept(client_sockfd, (struct sockaddr *)&client_addr,
      &addr_size);
    if(new_sockfd == -1) {
      if(DEBUG_MODE) { perror("accept"); }
      continue;
    }

    int err;
    pthread_t thread_id;
    if((err = pthread_create(&thread_id, &attr, thread_connect, &new_sockfd))) {
      if(DEBUG_MODE) { printf("Thread creation failed: %s\n", err); }
      close(new_sockfd);
    }
  }

  pthread_attr_destroy(&attr);  // code should not reach this point
  pthread_exit(NULL);

  signal(SIGTERM, clean_exit);
  signal(SIGINT, clean_exit);

  return 0;
}

/* Decrements the thread count atomically */
void decrement_thread_count() {
  pthread_mutex_lock(&count_mutex);
  --thread_count;
  if(DEBUG_MODE) { printf("Thread count decremented to %d\n", thread_count); }
  pthread_mutex_unlock(&count_mutex);
}

/* Increments the thread_count atomically and returns true if the resulting
 * count is within MAX_THREADS_SUPPORTED (inclusive); otherwise the count
 * remains unchanged and the function returns false */
bool increment_thread_count_successful() {
  pthread_mutex_lock(&count_mutex);
  bool success = false;
  if(thread_count < MAX_THREADS_SUPPORTED) {
    ++thread_count;
    success = true;
  }
  if(DEBUG_MODE) {
    if(!success) { printf("Thread count MAXED OUT at %d.\n", thread_count); }
    else { printf("Thread count incremented to %d\n", thread_count);}
  }
  pthread_mutex_unlock(&count_mutex);
  return success;
}

/* When successful, enables a thread to connect to a webserver, request data,
 * and return that data to a client. If unsuccessful, sends error to client. */
void* thread_connect (void * new_sockfd_ptr) {

  int new_sockfd = *((int*) new_sockfd_ptr);
  int webserv_sockfd;

  size_t size_request, size_headers, size_body; // size in bytes

  char req_buf[SMALL_BUFFERSIZE];
  char header_buf[BUFFERSIZE];
  char body_buf[BUFFERSIZE];

  std::string webserv_host, webserv_port;

  if(!get_msg_from_client(new_sockfd, req_buf, header_buf, body_buf,
    size_request, size_headers, size_body)) {
    close(new_sockfd);
    decrement_thread_count();
    pthread_exit(NULL);
  }

  // check if request and header lines are long enough to be valid
  // if(req_headers.size() <= strlen("GET http://x/ HTTP/1.0")) {
  //   close(new_sockfd);
  //   decrement_thread_count();
  //   Proxy_Error err = e_empty_req;
  //   send_error_to_client(new_sockfd, err);
  //   if(DEBUG_MODE) { printf("Rejected empty request line.\n"); }
  //   pthread_exit(NULL);
  // }

  std::string data, req_headers; //TODO

  // parse request and header lines
  if(!get_parsed_data(req_headers, webserv_host, webserv_port, data)) {
    if(DEBUG_MODE) {
      printf("+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");
      printf("PARSE ERROR:\n%s\n", data.c_str());
      printf("RECEIVED:\t%s\n", req_headers.c_str());
      printf("+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");
    }
    send_error_to_client(new_sockfd, data);
    close(new_sockfd);
    decrement_thread_count();
    pthread_exit(NULL);
  } // parsing successful; generated HTTP request ('data') for request/headers

  // connect to web server
  if(!connect_to_web_server(webserv_host, webserv_port, webserv_sockfd)) {
    Proxy_Error err = e_serv_connect;
    send_error_to_client(new_sockfd, err);
    close(new_sockfd);
    decrement_thread_count();
    pthread_exit(NULL);
  }

  // include body in web server request (if present)
  // if(size_body > 0) { data += body; }
  if(DEBUG_MODE) {
    printf("......................web server request.......................\n");
    printf("%s", data.c_str());
    printf("...............................................................\n");
  }

  // send HTTP request to web server
  int length = data.length();
  if(send_all(webserv_sockfd, (char*)data.c_str(), &length) == -1) {
    if(DEBUG_MODE) { perror("Sending HTTP request to server.\n\tsend_all"); }
    Proxy_Error err = e_serv_send;
    send_error_to_client(new_sockfd, err);
    close(new_sockfd);
    decrement_thread_count();
    pthread_exit(NULL);
  }

  // get response from webserver and redirect to client
  if(!send_webserver_data_to_client(webserv_sockfd, new_sockfd)) {
    close(new_sockfd);
    decrement_thread_count();
    pthread_exit(NULL);
  }

  // release client, transaction complete
  close(new_sockfd);
  decrement_thread_count();
  pthread_exit(NULL);
}

/* Assigns user specified port number and returns true if argument valid, i.e.
 * the number is between 10000 and 13000 (inclusive); otherwise returns false */
bool port_number_is_valid(int& port_int, int port_number_arg) {
  if(port_number_arg >= 10000 && port_number_arg <=13000) {
    port_int = port_number_arg;
    return true;
  }
  return false;
}

/* Converts port number into a usable format for socket connection */
void set_port_number(char* port_buf, int port_int) {
   int n = sprintf(port_buf, "%d", port_int);
   if(n < 5) {
     fprintf(stderr, "failed to set port number: %d\n", port_int);
     exit(EXIT_FAILURE);
   }
   if(DEBUG_MODE) { printf("Proxy connecting to port: %s\n", port_buf); }
 }

/* Creates a socket for the proxy and binds to it based on the defined port
 * number or prints error and exits on failure.
 * (Socket created is designated to listening for client requests) */
void create_and_bind_to_socket(int& client_sockfd, const char* port_buf) {
  struct addrinfo hints, *servinfo, *p;
  int rv;

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;  // use AF_INET6 to force IPv6
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;  // uses current IP address (cs2.seattleu.edu)

  if((rv = getaddrinfo(NULL, port_buf, &hints, &servinfo)) != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
    exit(EXIT_FAILURE);
  }

  // loop through all the results and connect to the first one possible
  for(p = servinfo; p != NULL; p = p->ai_next) {
    if((client_sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol))
        == -1) {
      perror("socket");
      continue;
    }
    if(bind(client_sockfd, p->ai_addr, p->ai_addrlen) == -1) {
      perror("bind");
      close(client_sockfd);
      continue;
    }
    break;  // if code reaches this point, connection was made successfully
  }
  freeaddrinfo(servinfo); // free memory

  // if p reached NULL, failed to bind with results in list
  if(p == NULL) {
    fprintf(stderr, "failed to bind socket\n");
    exit(EXIT_FAILURE);
  }
}

/* If successful, gets HTTP message from client, assigns result to strings
 * (one for the request and header lines: 'req_header_mssg', and one for the
 * body: 'body_mssg'), and returns true; otherwise returns false. */
bool get_msg_from_client(int client_sockfd, char (&req_buf)[SMALL_BUFFERSIZE],
  char (&header_buf)[BUFFERSIZE], char (&body_buf)[BUFFERSIZE],
  size_t& num_bytes_request, size_t& num_bytes_headers, size_t& num_bytes_body)
  {

  int numbytes = 0, totalbytes = 0, totalbytes_needed = 0;

  int num_bytes_req_headers = 0;

  num_bytes_request = 0;
  num_bytes_headers = 0;
  num_bytes_body = 0;

  char mssg_buf[BUFFERSIZE];
  char fullmssg_buf[BUFFERSIZE];
  memset(mssg_buf, 0, sizeof mssg_buf);
  memset(fullmssg_buf, 0, sizeof fullmssg_buf);

  memset(req_buf, 0, sizeof req_buf);
  memset(header_buf, 0, sizeof header_buf);
  memset(body_buf, 0, sizeof body_buf);

  size_t mssg_end_found;
  size_t req_end_found;

  std::string full_mssg;
  std::string mssg_end_used;  // (standard cr-lf-cr-lf or lf-lf only)
  std::string req_end_used;   // (standard cr-lf or lf only)

  bool end_detected = false;  // recognize end of req/headers (mssg_end)
  bool headers_detected = false;
  bool body_detected = false;
  bool message_complete = false;


  // retrieve request and optional header lines from client HTTP request
  while(!message_complete) {
    if((numbytes = recv(client_sockfd, mssg_buf, BUFFERSIZE-1, 0)) == -1) {
      if(DEBUG_MODE) { perror("Getting HTTP request from client.\n\trecv"); }
      return false;
    }
    if(numbytes == 0) { // closed TCP connection
      message_complete = true;
      continue;
    }
    totalbytes += numbytes;
    strcat(fullmssg_buf, mssg_buf);
    memset(mssg_buf, 0, sizeof mssg_buf);

    // check for end of message marker if not yet detected
    full_mssg = std::string(fullmssg_buf);
    if(!end_detected) {
      end_detected =
        req_header_end_detected(full_mssg, mssg_end_used, mssg_end_found,
          req_end_used, req_end_found, headers_detected);
      if(end_detected) {
        num_bytes_req_headers = mssg_end_found + mssg_end_used.size();
        size_t header_found = full_mssg.find(HTTP_BODY_HEADER);
        body_detected = header_found != std::string::npos;
        if(headers_detected) {
          num_bytes_request = req_end_found + req_end_used.size();
          num_bytes_headers = num_bytes_req_headers - num_bytes_request;
        }
        else {
          num_bytes_request = num_bytes_req_headers;
        }
        if(body_detected) {
          num_bytes_body = get_bytes_in_body(full_mssg, header_found);
          totalbytes_needed = num_bytes_req_headers + num_bytes_body;
        }
      } // else, end_detected == false (check again in next loop)
    }
    if(end_detected) {  // end_detected == true
      if(totalbytes >= totalbytes_needed) {
        message_complete = true;
      }
    }
  } // message_complete

  // assign values for request/header lines and body
  strncpy(req_buf, fullmssg_buf, num_bytes_request);
  if (headers_detected) {
    strncpy(header_buf, fullmssg_buf + num_bytes_request, num_bytes_headers);
  }
  if(num_bytes_req_headers <= full_mssg.size()) { // if body exists
    strncpy(body_buf, fullmssg_buf + num_bytes_req_headers, num_bytes_body);
  }
  if(DEBUG_MODE) {
    printf("_______________________________________________________________\n");
    printf("REQUEST: %s\n", req_buf);
    printf("HEADERs: %s\n", header_buf);
    printf("BODY: %s\n", body_buf);
    printf("_______________________________________________________________\n");
  }
  return true;
}

/* Checks the portion of the client message received for the end of the request
 * and header lines and returns true if found or false if not. */
bool req_header_end_detected(std::string& msg, std::string& end_used,
  size_t& msg_end_found, std::string& req_end_used, size_t& req_end_found,
  bool& headers_detected) {

  bool is_detected = false;

  std::string mssg_end = "\r\n\r\n"; // cr lf cr lf = standard
  std::string alt_mssg_end = "\n\n"; // gracefully handle lf lf only

  // find end of request/header message & the type used
  msg_end_found = msg.find(mssg_end);
  if(msg_end_found != std::string::npos) {
    end_used = mssg_end;
    is_detected = true;
  }
  else {
    msg_end_found = msg.find(alt_mssg_end);
    if(msg_end_found != std::string::npos) {
      end_used = alt_mssg_end;
      is_detected = true;
    }
  }

  // if end detected, find if request includes headers
  if(is_detected) {
    std::string req_msg = msg.substr(0, msg_end_found);
    std::string req_end = "\r\n"; // cr lf = standard
    std::string alt_req_end = "\n"; // gracefully handle lf only

    req_end_found = req_msg.find(req_end);
    if(req_end_found != std::string::npos) {
      req_end_used = req_end;
      headers_detected = true;
    }
    else {
      req_end_found = req_msg.find(alt_req_end);
      if(req_end_found != std::string::npos) {
        req_end_used = alt_req_end;
        headers_detected = true;
      }
    }
  }

  return is_detected;
}

/* Accepts the HTTP request starting at the first position of HTTP_BODY_HEADER
 * and returns the number of bytes in the body as indicated by the header. */
int get_bytes_in_body(std::string& msg, size_t header_start) {
  int num_bytes_in_body;
  // get past header name (<HEADER NAME>: <HEADER VALUE>)
  std::string bytes_start = msg.substr(header_start + HTTP_BODY_HEADER.size());
  // get past ":" ( varying spaces possible before/after -> '[*]:[*]')
  size_t colon_found = bytes_start.find(":");
  bytes_start = bytes_start.substr(colon_found + 1);
  // <HEADER VALUE> indicates the number of bytes in the body (ends with [\r]\n)
  std::istringstream iss(bytes_start.substr(0, bytes_start.find('\n')));
  iss >> num_bytes_in_body;
  return num_bytes_in_body;
}

/* Establishes connection to the client's requested web server */
bool connect_to_web_server(std::string webserv_host, std::string webserv_port,
  int& webserv_sockfd) {

  struct addrinfo hints, *servinfo, *p;
  int rv;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;  // use AF_INET6 to force IPv6
  hints.ai_socktype = SOCK_STREAM;

  if((rv = getaddrinfo(webserv_host.c_str(), webserv_port.c_str(), &hints,
    &servinfo)) != 0) {
    if(DEBUG_MODE) { fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv)); }
    return false;
  }
  struct timeval start, current;
  double seconds_passed;
  if(PREEMPT_EXIT && (atoi(webserv_port.c_str()) != STANDARD_PORT)) {
    gettimeofday(&start, NULL);
  }
  // loop through all the results and connect to the first one possible
  for(p = servinfo; p != NULL; p = p->ai_next) {
    if(PREEMPT_EXIT && (atoi(webserv_port.c_str()) != STANDARD_PORT)) {
      gettimeofday(&current, NULL);
      seconds_passed = (current.tv_sec - start.tv_sec);
      if(seconds_passed > MAX_SECONDS_TO_WAIT) {
        if(DEBUG_MODE) { printf("exited after %f seconds\n", seconds_passed); }
        return false; // failed to connect within time limit
      }
    }
    webserv_sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if(webserv_sockfd == -1) {
      if(DEBUG_MODE) { perror("(web server) socket"); }
      continue;
    }
    if(connect(webserv_sockfd, p->ai_addr, p->ai_addrlen) == -1) {
      if(DEBUG_MODE) { perror("(web server) connect"); }
      close(webserv_sockfd);
      continue;
    }
    break;  // if code reaches this point, connection was made successfully
  }

  if(p == NULL) {   // p reached NULL with no connection
    if(DEBUG_MODE) { fprintf(stderr, "failed to connect to server\n"); }
    return false;
  }

  freeaddrinfo(servinfo); // free memory
  return true;
}

/* Reads a message from a socket and sends it to a client */
bool send_webserver_data_to_client(int webserv_sockfd, int new_sockfd) {

  int numbytes;

  char mssg_buf[BUFFERSIZE];
  memset(mssg_buf, 0, sizeof mssg_buf);

  bool message_completed = false;

  while(!message_completed) {
    if((numbytes = recv(webserv_sockfd, mssg_buf, BUFFERSIZE-1, 0)) == -1) {
      if(DEBUG_MODE) { perror("Retrieving data from web server.\n\trecv"); }
      Proxy_Error err = e_serv_recv;
      send_error_to_client(new_sockfd, err);
      return false;
    }
    if(numbytes == 0) {
      message_completed = true;
    }
    else {
      mssg_buf[numbytes] = '\0';
      int length = strlen(mssg_buf);
      if(send_all(new_sockfd, mssg_buf, &length) == -1) {
        if(DEBUG_MODE) { perror("Sending server data to client.\n\tsend_all"); }
        return false;
      }
      memset(mssg_buf, 0, sizeof mssg_buf);
    }
  }

  return message_completed;
}

/* Sends a status of 'HTTP/1.0 500 Internal Error' in case of a *parse* error;
 * accepts a *string* (indicates parse error if parse messages turned off) */
void send_error_to_client(int& client_sockfd, std::string& parse_err) {
  std::string error_msg = STANDARD_ERROR_MESSAGE;
  if(INCLUDE_PROXY_ERROR_MSGS) {
    if(parse_err.empty()) { error_msg += "Error parsing HTTP request.\n"; }
    else { error_msg += parse_err; }
  }
  error_msg += "\n";
  int length = error_msg.length();
  if(send_all(client_sockfd, (char*)error_msg.c_str(), &length) == -1) {
    if(DEBUG_MODE) { perror("Sending error message to client.\n\tsend_all"); }
  }
}

/* Sends a status of 'HTTP/1.0 500 Internal Error' in case of a proxy error;
 * accepts an *enum* representing the error */
void send_error_to_client(int& client_sockfd, Proxy_Error& err) {
  std::string error_msg = STANDARD_ERROR_MESSAGE;
  if(INCLUDE_PROXY_ERROR_MSGS) { get_proxy_error_msg(error_msg, err); }
  error_msg += "\n";
  int length = error_msg.length();
  if(send_all(client_sockfd, (char*)error_msg.c_str(), &length) == -1) {
    if(DEBUG_MODE) { perror("Sending error message to client.\n\tsend_all"); }
  }
}

/* returns an appropriate error message (as a string) for the given err */
void get_proxy_error_msg(std::string msg, Proxy_Error& err) {
  switch(err) {
     case e_serv_connect:
      msg += "Connection to web server failed.\n";
      break;
     case e_serv_send:
      msg += "Failed to send HTTP request to server. Please retry request.\n";
      break;
     case e_serv_recv:
      msg += "Unable to redirect web server data. Please retry request.\n";
      break;
    case e_empty_req:
      msg += "Empty request line.\n";
      break;
  }
}

/* Handles partial sends - based on sample from beej: if successful, assigns
 * total_bytes_sent and returns 0; otherwise, returns -1 on failure */
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

  *length = total_bytes_sent;
  return numbytes == -1 ? -1 : 0;
}

/* Helps to ensure port is freed upoon a "rough" exit from a program */
void clean_exit(int flag) {
  exit(EXIT_SUCCESS);
}
