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

#include "parse.h"

const bool DEBUG_MODE = true;
const bool INCLUDE_PROXY_ERROR_MSGS = true;

const bool PREEMPT_EXIT = true;         // if true, exits if the time to connect
const int MAX_SECONDS_TO_CONNECT = 10;  // has exceeded MAX_SECONDS_TO_CONNECT
const int STANDARD_PORT = 80;           // (PREEMPT_EXIT is only activated if
                                        // port is not the STANDARD_PORT)

const int DEFAULT_PORT_NUMBER = 10042;
const int CONNECTIONS_ALLOWED = 5;    // TODO change to 30?
const int MAX_THREADS_SUPPORTED = 5;  // TODO change to 30.
                                      // note: is approximate, possible thread
                                      // count may fluctuate-> count decremented
                                      // just before pthread exits (could create
                                      // new thread in overlap - s/b ok)
const int BUFFERSIZE = 10000;
const int MAXDATASIZE = 100000;     // max size of client HTTP request

// TODO: exponential wait once MAX_THREADS_SUPPORTED is met (max 5-10 seconds?)
//       (resources get sucked up fast... looping through manage_thread_count)
// TODO: we should kick out clients who are inactive for 10+ seconds if possible
//       ...start the timer after accept and boot them if they don't have an
//       HTTP request in by that time
// TODO: below (the -1 initializer for thread_count) - I think that's what's
//       happening, but I don't really grok why... will read up on that
// TODO: we may want to reduce CONNECTIONS_ALLOWED.. For example, say we have
//       CONNECTIONS_ALLOWED = 5 and MAX_THREADS_SUPPORTED = 5, this means we
//       can have 5 threads running and 5 waiting, no one is denied a connection
//       nbd with 5, but 30? That could be a long wait w/ 30 and 30. May want to
//       start showing the connection failing before that to tip off the client
// ...after testing that... doesn't do a thing. error? check thread creation vs. count

int thread_count = -1;  // initial connection brings count to 0
enum Thread_Count_Action { add_thread, remove_thread };
pthread_mutex_t count_mutex = PTHREAD_MUTEX_INITIALIZER;

bool manage_thread_count(Thread_Count_Action& action);
void* thread_connect (void * new_sockfd_ptr);

bool port_number_is_valid(int& port_int, int port_number_arg);
void set_port_number(char* port_buf, int port_int);

void create_and_bind_to_socket(int& client_sockfd, const char* port_buf);
bool get_msg_from_client(int client_sockfd, std::string& client_msg);
bool connect_to_web_server(std::string webserv_host, std::string webserv_port,
    int& webserv_sockfd);
bool send_webserver_data_to_client(int webserv_sockfd, int new_sockfd);
void send_error_to_client(int& client_sockfd, std::string& custom_msg);

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

  // set threads to be detached (able to exit without joining)
  pthread_attr_t attr;
  int ret;
  if((ret = pthread_attr_init(&attr))) {
    printf("error %d: pthread_attr_init()\n", ret);
    exit(EXIT_FAILURE);
  }
  if((ret = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED))) {
    printf("error %d: pthread_attr_setdetachstate()\n", ret);
    exit(EXIT_FAILURE);
  }
  // TODO: verify reuse of attr is working - see pthread_attr_getdetachedstate()

  // accept incoming connections
  while(true) {
    int new_sockfd;
    struct sockaddr_storage client_addr;
    struct sigaction sa;
    socklen_t addr_size;
    addr_size = sizeof client_addr;
    Thread_Count_Action action = add_thread;

    // only allow MAX_THREADS_SUPPORTED to exist simultaneously
    while(!manage_thread_count(action)) {
     /* TODO: exponential waits X # ms -> 10 sec max? */
     //unsigned int usecs = (unsigned int)5000000;
     //usleep(usecs);
    }
    new_sockfd = accept(client_sockfd, (struct sockaddr *)&client_addr,
      &addr_size);
    if(new_sockfd == -1) {
      perror("accept");
      continue;
    }

    int err;
    pthread_t thread;
    if(DEBUG_MODE) { printf("Creating thread...\n"); }
    if((err = pthread_create(&thread, &attr, thread_connect, &new_sockfd))) {
      printf("Thread creation failed: %s\n", err);
      close(new_sockfd);
    }
  }

  pthread_attr_destroy(&attr);  // code should not reach this point
                                // (detached attr reused for all threads)
  signal(SIGTERM, clean_exit);
  signal(SIGINT, clean_exit);

  return 0;
}

/* (tracks the number of threads running at a given moment)
 * An action to decrement the thread count always succeeds and the function
 * returns true. An action to increment the thread count succeeds and returns
 * true if the resulting count is within MAX_THREADS_SUPPORTED (inclusive);
 * otherwise the count remains unchanged and the function returns false. */
bool manage_thread_count(Thread_Count_Action& action) {
  bool countOk = false;
  pthread_mutex_lock(&count_mutex);
  // always allow terminated threads to reduce thread_count
  if(action == remove_thread) {
    --thread_count;
    if(DEBUG_MODE) {
      if(thread_count < 0) {
        printf("thread_count negative: (%d)\n", thread_count);
      }
    }
    countOk = true;
  }
  // only increment thread_count if it will not exceed the limit
  else if(action == add_thread) {
    if(thread_count < MAX_THREADS_SUPPORTED) {
      ++thread_count;
      countOk = true;
    }
    if(DEBUG_MODE) {
      if(thread_count >= MAX_THREADS_SUPPORTED) {
        printf("Thread count maxed out at %d threads.\n", thread_count);
      }
      // TODO: remove once tested enough
      printf("Thread Count: %d\n", thread_count);
    }
  }
  pthread_mutex_unlock(&count_mutex);
  return countOk;
}

/* When successful, enables a thread to connect to a webserver, request data,
 * and return that data to a client. If unsuccessful, sends error to client. */
void* thread_connect (void * new_sockfd_ptr) {
  int new_sockfd = *((int*) new_sockfd_ptr);
  int webserv_sockfd;
  if(DEBUG_MODE) { printf("thread created...\n"); }
  std::string client_msg, webserv_host, webserv_port, data;
  if(!get_msg_from_client(new_sockfd, client_msg)) {
    std::string custom_msg = "Failed to read HTTP request. "
                             "Please try again.\n";
    send_error_to_client(new_sockfd, custom_msg);
    close(new_sockfd);
    Thread_Count_Action action = remove_thread;
    while(!manage_thread_count(action)) { }
    pthread_exit(NULL);
  }

  // if parse fails, send error message ('data') and close client connection
  if(!get_parsed_data(client_msg, webserv_host, webserv_port, data)) {
    if(DEBUG_MODE) { printf("client error message: %s\n", data.c_str()); }
    send_error_to_client(new_sockfd, data);
    close(new_sockfd);
    Thread_Count_Action action = remove_thread;
    while(!manage_thread_count(action)) { }
    pthread_exit(NULL);
  }

  // parsing successful; generated HTTP request ('data') received
  if(DEBUG_MODE) { printf("web server request:\n%s", data.c_str()); }

  // connect to web server or send error to and close client connection
  if(!connect_to_web_server(webserv_host, webserv_port, webserv_sockfd)) {
    std::string custom_msg = "Connection to web server failed.\n";
    send_error_to_client(new_sockfd, custom_msg);
    close(new_sockfd);
    Thread_Count_Action action = remove_thread;
    while(!manage_thread_count(action)) { }
    pthread_exit(NULL);
  }

  // connection to web server successful, send 'data' request to web server
  int length = data.length();
  if(send_all(webserv_sockfd, (char*)data.c_str(), &length) == -1) {
    perror("send_all");
    printf("Only sent %d bytes of HTTP request to web server.\n", length);
    std::string custom_msg = "Failed to send HTTP request to web server. "
                             "Please retry request.\n";
    send_error_to_client(new_sockfd, custom_msg);
    close(new_sockfd);
    Thread_Count_Action action = remove_thread;
    while(!manage_thread_count(action)) { }
    pthread_exit(NULL);
  }

  // attempt to retrieve data from webserver and redirect to client
  if(!send_webserver_data_to_client(webserv_sockfd, new_sockfd)) {
    std::string custom_msg = "Unable to redirect web server data. "
                             "Please retry request.\n";
    send_error_to_client(new_sockfd, custom_msg);
    close(new_sockfd);
    Thread_Count_Action action = remove_thread;
    while(!manage_thread_count(action)) { }
    pthread_exit(NULL);
  }

  close(new_sockfd);  // release client, transaction successful
  Thread_Count_Action action = remove_thread;
  while(!manage_thread_count(action)) { }
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
   if(DEBUG_MODE) {
     printf("Proxy connecting to port number: %s\n", port_buf);
   }
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

  // if p reached NULL with no bind, print error and exit
  if(p == NULL) {
    fprintf(stderr, "failed to bind socket\n");
    exit(EXIT_FAILURE);
  }
}

/* Receives HTTP message from client and returns as a string; marks the end of
 * the message with two repeating newlines (4 characters: '\r\n\r\n') */
bool get_msg_from_client(int client_sockfd, std::string& client_msg) {
  int numbytes = 0, totalbytes = 0;
  char mssg_buf[BUFFERSIZE];
  char fullmssg_buf[MAXDATASIZE];
  memset(mssg_buf, 0, sizeof mssg_buf);
  memset(fullmssg_buf, 0, sizeof fullmssg_buf);
  char msg_end[] = { '\r', '\n', '\r', '\n' };
  int end_size = sizeof(msg_end);
  bool message_completed = false;
  if(DEBUG_MODE) { printf("\nRetrieving message...\n"); }
  // retrieve message from client
  while(!message_completed) {
    if((numbytes = recv(client_sockfd, mssg_buf, BUFFERSIZE-1, 0)) == -1) {
      perror("recv");
      printf("Receiving HTTP request from client\n");
      return false;
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
  client_msg = std::string(fullmssg_buf);
  return true;
}

/* Establishes connection to client's requested web server */
bool connect_to_web_server(std::string webserv_host, std::string webserv_port,
    int& webserv_sockfd) {
  struct addrinfo hints, *servinfo, *p;
  int rv;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;  // use AF_INET6 to force IPv6
  hints.ai_socktype = SOCK_STREAM;

  if((rv = getaddrinfo(webserv_host.c_str(), webserv_port.c_str(), &hints,
      &servinfo)) != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
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
      if(seconds_passed > MAX_SECONDS_TO_CONNECT) {
        if(DEBUG_MODE) { printf("exited after %f seconds\n", seconds_passed); }
        return false; // failed to connect within time limit
      }
    }
    if((webserv_sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol))
        == -1) {
      perror("web server socket");
      continue;
    }
    if(connect(webserv_sockfd, p->ai_addr, p->ai_addrlen) == -1) {
      perror("web server connect");
      close(webserv_sockfd);
      continue;
    }
    break;  // if code reaches this point, connection was made successfully
  }

  if(p == NULL) {   // p reached NULL with no connection
    fprintf(stderr, "failed to connect\n");
    return false;
  }
  freeaddrinfo(servinfo); // free memory

  return true;
}

/* Reads a message from a socket and sends it to a client */
bool send_webserver_data_to_client(int webserv_sockfd, int new_sockfd) {
  int numbytes;
  char mssg_buf[BUFFERSIZE];
  std::string proxy_string;
  bool message_completed = false;
  while(!message_completed) {
    if((numbytes = recv(webserv_sockfd, mssg_buf, BUFFERSIZE-1, 0)) == -1) {
      perror("recv");
      printf("Retrieving data from web server");
      return false;
    }
    if(numbytes == 0) {
      message_completed = true;
      proxy_string = "\n";
    }
    else {
      mssg_buf[numbytes] = '\0';
      proxy_string = mssg_buf;
    }
    int length = proxy_string.length();
    if(send_all(new_sockfd, (char*)proxy_string.c_str(), &length) == -1) {
      perror("send_all");
      printf("Only sent %d bytes of web server data to client.\n", length);
      return false;
    }
  }
  return message_completed;
}

/* sends a status of 'HTTP/1.0 500 Internal Error' in the case of a failed
 * connection to the web server */
void send_error_to_client(int& client_sockfd, std::string& custom_msg) {
  std::string error_msg = "HTTP/1.0 500 Internal error\n";
  if(INCLUDE_PROXY_ERROR_MSGS) {
    error_msg += custom_msg;
  }
  error_msg += "\n";
  int length = error_msg.length();
  if(send_all(client_sockfd, (char*)error_msg.c_str(), &length) == -1) {
    perror("send_all");
    printf("Only sent %d bytes of error message to client.\n", length);
  }
}

/* Handles partial sends - based on sample from beej */
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

/* Helps to ensure port is freed upoon a "rough" exit from a program */
void clean_exit(int flag) {
  exit(EXIT_SUCCESS);
}
