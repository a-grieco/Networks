/* parse.cpp */
// used by proxy for parsing client HTTP requests

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <iostream>
#include <errno.h>
#include <vector>
#include <sstream>
#include <iterator>

#include "parse.h"

const bool DEBUG_MODE = true;
const bool INCLUDE_PARSING_ERROR_MSGS = true;

const std::string DEFAULT_PORT = "80";
const std::string VALID_METHOD = "GET";
const std::string VALID_HTTP_VERS = "HTTP/1.0";
const std::string VALID_URL_PROTOCOL_PREFIX = "http:";

// NOTE: get_parsed_data should be the only function the proxy needs to call

/* accepts a client message, assigns a valid web server request to 'data', and
 * returns true if the client message is valid; otherwise, assigns appropriate
 * detailed error message to 'data' and returns false */
bool get_parsed_data(int req_size, int header_size,
  char (&req_buf)[SMALL_BUFFERSIZE], char(&header_buf)[BUFFERSIZE],
  std::string& webserv_host, std::string& webserv_port, std::string& data) {

  std::string host, path, port;
  std::string header_str = std::string(header_buf);
  Parse_Error err;

  data = std::string(req_buf);

  if(parse_request_line(data, host, path, port, err)) {
    webserv_host = host;
    webserv_port = port;
    printf("data after parse_request_line %s\n", data.c_str());
    if(header_size > 0 && parse_headers(header_buf, header_size, err)) {
      generate_webserver_request(data, host, path, port);
      printf("data!!!! &s\n", data.c_str());
      return true;
    }
    else {
      generate_webserver_request(data, host, path, port);
      return true;
    }    
  }

  // otherwise, if parse unsuccessful, return false
  if(DEBUG_MODE) { printf("in <parse.cpp> parse_client_msg detected error\n"); }
  if(INCLUDE_PARSING_ERROR_MSGS) { get_parse_error_msg(data, err); }
  return false;

}

/* **************************** HELPER FUNCTIONS **************************** */

/* generates a formated HTTP request for the web server requested */
void generate_webserver_request(std::string& data, std::string& host,
  std::string& path, std::string& port) {
  std::string method = VALID_METHOD, http_vers = VALID_HTTP_VERS;
  std::string webserv_req;
  if(is_match_caseins(DEFAULT_PORT, port)) {
    webserv_req= method + " /" + path + " " + http_vers + "\nHost: " + host +
      "\nConnection: close\n";
  }
  else {  // include port # in Host: header if not default port '80'
    webserv_req= method + " /" + path + " " + http_vers + "\nHost: " + host +
      ":" + port + "\nConnection: close\n";
  }
  webserv_req += "\n";
  data = webserv_req;
}

/* generates a detail for the precise cause of the error */
void get_parse_error_msg(std::string& data, Parse_Error& err) {
  switch(err) {
    case e_req_line:
      data += "Invalid request line: expecting <METHOD> <URL> <HTTP VERSION>"
              "\ni.e. 'GET http://hostname[:port]/path HTTP/1.0'\n";
      break;
    case e_method:
      data += "Invalid HTTP method: only GET accepted\n";
      break;
    case e_url:
      data += "Invalid URL: must use absolute URI formatted as "
              "http://hostname[:port]/path\n";
      break;
    case e_http_vers:
      data += "Invalid HTTP version: only HTTP/1.0 accepted\n";
      break;
    case e_http_prefix:
      data += "Invalid protocol prefix in URL: only 'http://' accepted\n";
      break;
    case e_host:
      data += "Missing host\n";
      break;
    case e_dns:
      data += "Failed to resolve the hostname with DNS\n";
      break;
    case e_path:
      data += "Missing path: absolute URI required\n"
              "i.e. http://hostname[:port]/path\n";
      break;
    case e_port:
      data += "Invalid port number, must be numeric\n";
      break;
    case e_headers:
      data += "Invalid formatting of header(s). Expected <NAME>: <VALUE>\n";
      break;
    case e_name_ws:
      data += "Header name may not contain embedded whitespace,\n"
              "i.e. 'Content-type' ok, 'Content type' results in error\n";
      break;
    case e_header_val:
      data += "Header missing value. Expected <NAME>: <VALUE>\n";
      break;
  }
}

// PARSING FUNCTIONS

/* parses client request line into host, path, and port number and returns true;
 * otherwise, returns false for invalid formatting and/or bad DNS */
bool parse_request_line(std::string& req, std::string& host, std::string& path,
    std::string &port, Parse_Error& err) {

  // client request line should contain 3 parts: <METHOD> <URL> <HTTP VERSION>
  std::string method, url, http_vers;

  printf("REQUEST IN parse_request_line %s\n", req.c_str());

  if(!extract_request_elements(req, method, url, http_vers, err)) {
    return false; printf("EXTRACT ELEMENTS FAILED___________\n");
  }
  printf("extract_request_elements returned true\n");
  if(!verify_method(method)) {
    err = e_method;
    return false;
  }
  printf("VERIFY METHOD OK\n" );
  if(!verify_http_vers(http_vers)) {
    err = e_http_vers;
    return false;
  }
  printf("VERIFY HTTP VERSION OK\n" );
  if(!parse_url(url, host, path, port, err)) {
    return false;
  }
  printf("VERIFY URL OK\n");
  return true;
}

/* parses headers into a vector of strings (each element as <name: value>) and
 * returns true; otherwise returns false for invalid formatting
 * Note: if no headers are found, returns true with headers.empty() == true */
bool parse_headers(char(&header_buf)[BUFFERSIZE], int header_size,
  Parse_Error& err)
{
  std::string headers_str;
  std::vector<std::string> headers;
  std::vector<int> size_so_far;
  if(!extract_headers(headers_str, headers, size_so_far)) {
    return false;
  }
  if(!verify_headers(headers, err, size_so_far, header_buf, header_size)) {
    return false;
  }
  return true;     // no headers present
}

/* verifies that the url is correctly formatted and parses the web server's
 * host, path, and port number; otherwise returns false */
bool parse_url(std::string url, std::string& host, std::string& path,
    std::string& port, Parse_Error& err) {
  trim(url);

  if(!extract_and_verify_http_prefix(url)) {
    err = e_http_prefix;
    return false;
  }

  if(!extract_host_and_port(url, host, port, err)) {
    return false;
  }

  //DNS host verification
  struct addrinfo hints, *servinfo, *p;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  if(getaddrinfo(host.c_str(), port.c_str(), &hints, &servinfo) != 0) {
    err = e_dns;
    return false;
  }
  freeaddrinfo(servinfo); // free memory (DNS verification successufl)

  path = url;
  return true;
}

/* extracts each header line from 'msg' into an element in 'headers' and returns
 * true if headers are present; otherwise returns false */
bool extract_headers(std::string h_str, std::vector<std::string> &headers,
  std::vector<int> size_so_far) {

  std::string eol_delim_used;
  std::string eol_delim = "\r\n";   // standard is cr lf
  std::string alt_eol_delim = "\n"; // handle (non-standard) lf
  std::size_t pos = 0;
  int curr_size = 0;

  if(h_str.empty()) { return false; }

  bool all_headers_extracted = false;
  while(!all_headers_extracted && !h_str.empty()) {
    pos = h_str.find(eol_delim);
    eol_delim_used = eol_delim;
    if(pos == std::string::npos) {
      pos = h_str.find(alt_eol_delim);
      eol_delim_used = alt_eol_delim;
    }
    if(pos != std::string::npos) {
      if(pos > 0) {
        curr_size += (pos + eol_delim_used.length());
        size_so_far.push_back(curr_size);
        headers.push_back(h_str.substr(0, pos + eol_delim_used.length()));
      }
      else {
        all_headers_extracted = true;
      }
      h_str.erase(0, pos + eol_delim_used.size());
    }
    else {
      all_headers_extracted = true;
    }
  }
  return true;
}

/* extracts three space-delimited elements from the client request line: METHOD,
 * URL, and HTTP VERSION, assigns elements to respective variables, and returns
 * true; otherwise, returns false if there are more/fewer than three elements */
bool extract_request_elements(std::string req, std::string& method,
    std::string& url, std::string& http_vers, Parse_Error& err) {
  trim(req);
  printf("REQUEST IN extract_request_elements %s\n", req.c_str());
  // extract each space-delimited element into a vector entry
  std::istringstream iss(req);
  std::istream_iterator<std::string> beg(iss), end;
  std::vector<std::string> elements(beg, end);

  printf("ELEMENTS IN elements\n");
  for(int i = 0; i < elements.size(); i++) {
    printf("%s\n", (elements.at(i)).c_str());
  }

  // check that there are exactly three elements
  int num_elements = elements.size();
  if(num_elements != 3) {
    printf("NUM ELEMENTS %d", num_elements);
    err = e_req_line;
    return false;
  }

  // assign each element to its respective variable
  method = elements.at(0);
  url = elements.at(1);
  http_vers = elements.at(2);

  return true;
}

/* extracts and removes the host and port from a url formatted as [host(:port)]
 * and returns true; otherwise returns false if no 'host(:port)' is found */
bool extract_host_and_port(std::string& url, std::string& host,
  std::string& port, Parse_Error& err) {
  std::string host_delim = "/";
  std::size_t pos = url.find(host_delim);

  host = url.substr(0, pos);
  if(pos == std::string::npos) {
    url.erase(0, std::string::npos);  // no path included, url empty
  }
  else {
    url.erase(0, pos + host_delim.size());  // remaining url is path
  }
  trim(host);
  if(host.length() <= 0) {
    err = e_host;
    return false; // no host found
  }
  return extract_and_verify_port(host, port, err);
}

/* verifies the protocol prefix of the given url is 'http://', removes the
 * prefix from the url, and returns true; otherwise returns false for an invalid
 * url protocol prefix */
bool extract_and_verify_http_prefix(std::string& url) {
  std::string http_delim = "//";
  std::string valid_http_prefix = VALID_URL_PROTOCOL_PREFIX;
  std::string http_prefix;
  std::size_t pos = url.find(http_delim);

  if(pos == std::string::npos) { return false; }  // no '<prefix>//...' found
  http_prefix = url.substr(0, pos);
  url.erase(0, pos + http_delim.size());

  return is_match_caseins(valid_http_prefix, http_prefix);
}

/* accepts a host and port formatted as 'host' or 'host:port', respectively,
 * assigns each to the appropriate variable, assigns default port number if none
 * is present, and returns true; otherwise returns false for an invalid port */
bool extract_and_verify_port(std::string& host, std::string& port,
    Parse_Error& err) {
  std::string port_delim = ":";
  std::size_t pos = host.find(port_delim);

  // if no port included, assign default port
  if(pos == std::string::npos) {
    port = DEFAULT_PORT;
    return true;  // host retains full value
  }

  // if port included, divide from host
  port = host.substr(pos + port_delim.size());
  host = host.substr(0, pos);
  if(!verify_port(port)) {
    err = e_port;
    return false;
  }
  return true;
}

/* verifies that method is accepted (only GET in this assignment) and formatted
 * in uppercase; otherwise returns false on mismatch */
bool verify_method(std::string& method) {
  trim(method);
  return is_match_caseins(VALID_METHOD, method);
}

/* verifies that the HTTP version is accepted (only HTTP/1.0 in this assignment)
 * and formatted in uppercase; otherwise returns false on mismatch */
bool verify_http_vers(std::string& http_vers) {
  trim(http_vers);
  return is_match_caseins(VALID_HTTP_VERS, http_vers);
}

/* returns true if each header has valid format [name: value]; otherwise returns
 * false -- note: removes any duplicate 'Host' or 'Connection' headers */
bool verify_headers(std::vector<std::string> &headers, Parse_Error& err,
  std::vector<int> &size_so_far, char(&header_buf)[BUFFERSIZE], int header_size)
{
  std::size_t pos = 0;
  std::string name_delim = ":";
  std::string def_host = "Host", def_conn = "Connection";
  std::vector<int> dup_headers_found;

  std::string orig_header, name, value;

  printf("SIZES MATCH? %d %d\n", headers.size(), size_so_far.size());

  int num_elem = headers.size();
  for(int i = 0; i < num_elem; ++i) {

    orig_header = headers.at(i);
    pos = orig_header.find(name_delim);
    if(pos == std::string::npos) {  // missing ':'
      err = e_headers;
      return false;
    }

    name = orig_header.substr(0, pos);
    trim(name);

    // track indices of headers with names: 'Host' and/or 'Connection'
    if(is_match_caseins(def_host, name) || is_match_caseins(def_conn, name)) {
      dup_headers_found.push_back(i);
    }
  }

  // remove duplicate 'Host' (extracted from url) and 'Connection' (defaults to
  // close) headers -> these are automatically added to the web server request
  if(dup_headers_found.size() > 0) {
    char first_half[SMALL_BUFFERSIZE];
    char last_half[SMALL_BUFFERSIZE/2];
    size_t start, end, size;
    size_t total_size;
    for(int i = dup_headers_found.size()-1; i >=0; --i) {
      end = size_so_far.at(i);
      start = end - (headers.at(i)).length();
      size = end - start;
      strncpy(first_half, header_buf, start);  // copy all before
      strncpy(last_half, (header_buf + end), (total_size - end));
      strcat(first_half, last_half);
      memset(header_buf, 0, total_size);
      total_size -= size;
      strncpy(header_buf, first_half, total_size);
    }
  }
  return true;
}

/* returns true if the port is numeric (non-decimal) */
bool verify_port(std::string& port) {
  std::string nums = "0123456789";
  std::size_t pos = port.find_first_not_of(nums);
  return (pos == std::string::npos);
}

/* if two strings match (case insensitive), 'entry' is assigned prefered
 * formatting and function returns true; otherwise returns false on mismatch */
bool is_match_caseins(std::string valid, std::string& entry) {
   if(entry.length() != valid.length()) { return false; }
   for(int i = 0; i < valid.length(); ++i) {
     if(toupper(entry[i]) != toupper(valid[i])) { return false; }
   }
   entry = valid;
   return true;
 }

/* trims leading and trailing whitespace from a string */
void trim(std::string& str) {
  std::string whitespace = " \r\n\t";
 	str.erase(0, str.find_first_not_of(whitespace));
 	str.erase(str.find_last_not_of(whitespace) + 1);
}
