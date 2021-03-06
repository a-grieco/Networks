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

const bool DEBUG_MODE = false;
const bool INCLUDE_PARSING_ERROR_MSGS = false;

const std::string DEFAULT_PORT = "80";
const std::string VALID_METHOD = "GET";
const std::string VALID_HTTP_VERS = "HTTP/1.0";
const std::string VALID_URL_PROTOCOL_PREFIX = "http:";

// NOTE: get_parsed_data should be the only function the proxy needs to call

/* accepts a client message, assigns a valid web server request to 'data', and
 * returns true if the client message is valid; otherwise, assigns appropriate
 * detailed error message to 'data' and returns false */
bool get_parsed_data(std::string client_msg, std::string& webserv_host,
  std::string& webserv_port, std::string& data) {

  std::string host, path, port;
  std::vector<std::string> headers;
  Parse_Error err;

  // if parse successful, generate web server request and return true
  if(parse_client_msg(client_msg, host, path, port, headers, err)) {
    webserv_host = host;
    webserv_port = port;
    if(DEBUG_MODE) {
      printf("in <parse.cpp> parse_client_msg successful\r\n");
      printf("\thost: %s\r\n\tpath: %s\r\n\tport: %s\r\n\theaders:\r\n",
        host.c_str(), path.c_str(), port.c_str());
      for(int i = 0; i < headers.size(); ++i) {
        printf("\t%s%s%s\r\n", i == 0 ? "[" : " ", headers.at(i).c_str(),
          i == (headers.size()-1) ? "]" : " ");
      }
    }
    generate_webserver_request(data, host, path, port, headers);
    return true;
  }

  // otherwise, if parse unsuccessful, return false
  if(DEBUG_MODE) { printf("in <parse.cpp> parse_client_msg detected error\r\n"); }
  if(INCLUDE_PARSING_ERROR_MSGS) { get_parse_error_msg(data, err); }
  return false;
}

/* **************************** HELPER FUNCTIONS **************************** */

/* generates a formated HTTP request for the web server requested */
void generate_webserver_request(std::string& data, std::string& host,
  std::string& path, std::string& port, std::vector<std::string>& headers) {
  std::string method = VALID_METHOD, http_vers = VALID_HTTP_VERS;
  std::string webserv_req;
  if(is_match_caseins(DEFAULT_PORT, port)) {
    webserv_req= method + " /" + path + " " + http_vers + "\r\nHost: " + host +
      "\r\nConnection: close\r\n";
  }
  else {  // include port # in Host: header if not default port '80'
    webserv_req= method + " /" + path + " " + http_vers + "\r\nHost: " + host +
      ":" + port + "\r\nConnection: close\r\n";
  }
  for(std::vector<std::string>::iterator it = headers.begin();
    it != headers.end(); ++it) {
      webserv_req += *it + "\r\n";
  }
  webserv_req += "\r\n";
  data = webserv_req;
}

/* generates a detail for the precise cause of the error */
void get_parse_error_msg(std::string& data, Parse_Error& err) {
  switch(err) {
    case e_req_line:
      data += "Invalid request line: expecting <METHOD> <URL> <HTTP VERSION>"
              "\r\ni.e. 'GET http://hostname[:port]/path HTTP/1.0'\r\n";
      break;
    case e_method:
      data += "Invalid HTTP method: only GET accepted\r\n";
      break;
    case e_url:
      data += "Invalid URL: must use absolute URI formatted as "
              "http://hostname[:port]/path\r\n";
      break;
    case e_http_vers:
      data += "Invalid HTTP version: only HTTP/1.0 accepted\r\n";
      break;
    case e_http_prefix:
      data += "Invalid protocol prefix in URL: only 'http://' accepted\r\n";
      break;
    case e_host:
      data += "Missing host\r\n";
      break;
    case e_dns:
      data += "Failed to resolve the hostname with DNS\r\n";
      break;
    case e_path:
      data += "Missing path: absolute URI required\r\n"
              "i.e. http://hostname[:port]/path\r\n";
      break;
    case e_port:
      data += "Invalid port number, must be numeric\r\n";
      break;
    case e_headers:
      data += "Invalid formatting of header(s). Expected <NAME>: <VALUE>\r\n";
      break;
    case e_header_incomplete:
      data += "Header incomplete. Expected <NAME>: <VALUE>\r\n";
      break;
  }
}

// PARSING FUNCTIONS

/* extracts web server host, path, and port number and headers (if present) from
 * the client message and returns true if successful; otherwise returns false */
bool parse_client_msg(std::string msg, std::string& host, std::string& path,
  std::string& port, std::vector<std::string>& headers, Parse_Error& err) {

  // get first line of client request
  std::string delimiter = "\r\n";   // standard cr lf
  std::string alt_delimiter = "\n"; // handle (non-standard) lf
  std::size_t pos = msg.find(delimiter);
  if(pos == std::string::npos) {
    delimiter = alt_delimiter;
    pos = msg.find(delimiter);
  }
  std::string req = msg.substr(0, pos);
  msg.erase(0, pos + delimiter.size());

  if(parse_request_line(req, host, path, port, err)) {
    // request line parse successful, extract headers if present
    if(msg.size() > 0) {
      return(parse_headers(msg, headers, err));
    }
    return true;  // client request line valid
  }
  return false;   // client request line invalid
}

/* parses client request line into host, path, and port number and returns true;
 * otherwise, returns false for invalid formatting and/or bad DNS */
bool parse_request_line(std::string req, std::string& host, std::string& path,
    std::string &port, Parse_Error& err) {

  // client request line should contain 3 parts: <METHOD> <URL> <HTTP VERSION>
  std::string method, url, http_vers;

  if(extract_request_elements(req, method, url, http_vers, err)) {
    if(!verify_method(method)) {
      err = e_method;
      return false;
    }
    if(!verify_http_vers(http_vers)) {
      err = e_http_vers;
      return false;
    }
    if(!parse_url(url, host, path, port, err)) {
      return false;
    }
    return true;
  }
  return false;
}

/* parses headers into a vector of strings (each element as <name: value>) and
 * returns true; otherwise returns false for invalid formatting
 * Note: if no headers are found, returns true with headers.empty() == true */
bool parse_headers(std::string msg, std::vector<std::string> &headers,
    Parse_Error& err) {
  if(extract_headers(msg, headers)) {
    return verify_headers(headers, err);
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
bool extract_headers(std::string msg, std::vector<std::string> &headers) {
  std::string eol_delim_used;
  std::string eol_delim = "\r\n";   // standard is cr lf
  std::string alt_eol_delim = "\n"; // handle (non-standard) lf
  std::size_t pos = 0;

  if(msg.empty()) { return false; }

  bool all_headers_extracted = false;
  while(!all_headers_extracted && !msg.empty()) {
    pos = msg.find(eol_delim);
    eol_delim_used = eol_delim;
    if(pos == std::string::npos) {
      pos = msg.find(alt_eol_delim);
      eol_delim_used = alt_eol_delim;
    }
    if(pos != std::string::npos) {
      if(pos > 0) {
        headers.push_back(msg.substr(0, pos));
      }
      else {
        all_headers_extracted = true;
      }
      msg.erase(0, pos + eol_delim_used.size());
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
  // extract each space-delimited element into a vector entry
  std::istringstream iss(req);
  std::istream_iterator<std::string> beg(iss), end;
  std::vector<std::string> elements(beg, end);

  // check that there are exactly three elements
  int num_elements = elements.size();
  if(num_elements != 3) {
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

/* returns true if each header has valid format [name: value] and removes any
 * duplicate 'Host' or 'Connection' headers; otherwise returns false */
bool verify_headers(std::vector<std::string> &headers, Parse_Error& err) {
  std::size_t pos = 0;
  std::string name_delim = ":";
  std::string def_host = "Host", def_conn = "Connection",
    def_proxy_conn = "Proxy-Connection";
  std::vector<int> dup_headers_found;

  std::string orig_header, name, value;

  int num_elem = headers.size();
  for(int i = 0; i < num_elem; ++i) {

    orig_header = headers.at(i);
    pos = orig_header.find(name_delim);
    if(pos == std::string::npos) {  // missing ':'
      err = e_headers;
      return false;
    }

    if(pos == 0 || pos >= orig_header.length()) {
      err = e_header_incomplete;
      return false;
    }

    name = orig_header.substr(0, pos);
    value = orig_header.substr(pos);  // includes ":"

    headers.at(i) = name + value;

    // track indices of headers with names: 'Host' and/or 'Connection'
    if(is_match_caseins(def_host, name) || is_match_caseins(def_conn, name)
        || is_match_caseins(def_proxy_conn, name)) {
      dup_headers_found.push_back(i);
    }
  }

  // remove duplicate 'Host' (extracted from url) and 'Connection' (defaults to
  // close) headers -> these are automatically added to the web server request
  for(std::vector<int>::reverse_iterator rit = dup_headers_found.rbegin();
      rit != dup_headers_found.rend(); ++rit) {
    headers.erase(headers.begin() + *rit);
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
