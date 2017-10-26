/* parse.cpp */
// used by proxy for parsing client HTTP requests

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <iostream>
#include <errno.h>
#include <vector>
#include <sstream>
#include <iterator>

#include "parse.h"

#define DEBUG_MODE true
#define INCLUDE_CUSTOM_ERROR_MSGS true  // if false, use only 500 Internal Error
#define PATH_REQUIRED false   // false: uses default "GET / HTTP/1.0" format
                              //        if client message has no defined path
                              // true: client message is declared invalid with
                              //       no path defined
#define DEFAULT_PORT "80"
#define VALID_METHOD "GET"
#define VALID_HTTP_VERS "HTTP/1.0"
#define VALID_URL_PROTOCOL_PREFIX "http:"
#define INTERNAL_ERROR "500"

// NOTE: get_parsed_data should be the only function the proxy needs to call

/* accepts a client message, assigns a valid web server request to 'data', and
 * returns true if the client message is valid; otherwise, assigns appropriate
 * (500) error message to 'data' and returns false */
bool get_parsed_data(std::string client_msg, std::string& webserv_host,
  std::string& webserv_port, std::string& data) {

  std::string host, path, port;
  std::vector<std::string> headers;
  std::vector<Error> errnos;

  // if parse successful, generate web server request and return true
  if(parse_client_msg(client_msg, host, path, port, headers, errnos)) {
    webserv_host = host;
    webserv_port = port;
    if(DEBUG_MODE) {
      printf("in <parse.cpp> parse_client_msg successful\n");
      printf("\thost: %s\n\tpath: %s\n\tport: %s\n\theaders:\n",
        host.c_str(), path.c_str(), port.c_str());
      for(int i = 0; i < headers.size(); ++i) {
        printf("\t%s%s%s\n", i == 0 ? "[" : " ", headers.at(i).c_str(),
          i == (headers.size()-1) ? "]" : " ");
      }
    }
    generate_webserver_request(data, host, path, headers);
    return true;
  }

  // otherwise, generate error message and return false
  if(DEBUG_MODE) { printf("in <parse.cpp> parse_client_msg detected error\n"); }
  generate_client_error_msg(data, errnos);
  return false;
}

/* **************************** HELPER FUNCTIONS **************************** */

/* generates a formated HTTP request for the web server requested */
void generate_webserver_request(std::string& data, std::string& host,
  std::string& path, std::vector<std::string>& headers) {
  std::string method = VALID_METHOD, http_vers = VALID_HTTP_VERS;
  std::string webserv_req = method + " /" + path + " " + http_vers +
    "\nHost: " + host + "\nConnection: close\n";
  for(std::vector<std::string>::iterator it = headers.begin();
    it != headers.end(); ++it) {
      webserv_req += *it + "\n";
  }
  webserv_req += "\n";
  data = webserv_req;
}

/* generates a status line for the client with HTTP version: HTTP/1.0, response
 * status code: 500, and reason phrase: 'Internal Error'
 * Note: in DEBUG_MODE, more precise cause of error is included */
void generate_client_error_msg(std::string& data, std::vector<Error>& errnos) {
  std::string http_vers = VALID_HTTP_VERS, internal_errno = INTERNAL_ERROR;
  data = http_vers + " " + internal_errno + " Internal Error\n";

  if(DEBUG_MODE) { include_error_detail(data, errnos); }
}

/* adds additional information to client error message for debugging purposes */
void include_error_detail(std::string& data, std::vector<Error>& errnos) {
  for(std::vector<Error>::iterator it = errnos.begin(); it != errnos.end();
      ++it) {
    switch(*it) {
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
        data += "Invalid protocol prefix in URL: only 'http://'' accepted\n";
        break;
      case e_host:
        data += "Missing host\n";
        break;
      case e_dns:
        data += "Invalid host\n";
        break;
      case e_path:
        data += "Missing path\n";
        break;
      case e_port:
        data += "Invalid port number, must be numeric\n";
        break;
      case e_headers:
        data += "Invalid formatting of header(s): should be [name: value] "
                "where name has no interrupting whitespace\n";
        break;
    }
  }
}

// PARSING FUNCTIONS

/* extracts web server host, path, and port number and headers (if present) from
 * the client message and returns true if successful; otherwise returns false */
bool parse_client_msg(std::string msg, std::string& host, std::string& path,
    std::string& port, std::vector<std::string>& headers,
    std::vector<Error>& errnos) {

  // get first line of client request
  std::string delimiter = "\r\n";
  std::size_t pos = msg.find(delimiter);
  std::string req = msg.substr(0, pos);
  msg.erase(0, pos + delimiter.size());

  if(parse_request_line(req, host, path, port, errnos)) {
    // extract headers if present
    if(msg.size() > 0) {
      return(parse_headers(msg, headers, errnos));
    }
    return true;  // client request line valid
  }
  return false;   // client request line invalid
}

/* parses client request line into host, path, and port number and returns true;
 * otherwise, returns false for invalid formatting and/or bad DNS */
bool parse_request_line(std::string req, std::string& host, std::string& path,
    std::string &port, std::vector<Error>& errnos) {

  // client request line should contain 3 parts: <METHOD> <URL> <HTTP VERSION>
  std::string method, url, http_vers;

  if(extract_request_elements(req, method, url, http_vers, errnos)) {
    if(!verify_method(method)) {
      errnos.push_back(e_method);
      return false;
    }
    if(!verify_http_vers(http_vers)) {
      errnos.push_back(e_http_vers);
      return false;
    }
    if(!parse_url(url, host, path, port, errnos)) {
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
    std::vector<Error>& errnos) {

  if(extract_headers(msg, headers)) {
    if(!verify_headers(headers)) {
      errnos.push_back(e_headers);
      return false; // headers invalid
    }
  }
  return true;     // no headers present or headers valid
}

/* verifies that the url is correctly formatted and parses the web server's
 * host, path, and port number; otherwise returns false */
bool parse_url(std::string url, std::string& host, std::string& path,
    std::string& port, std::vector<Error>& errnos) {
  trim(url);

  if(!extract_and_verify_http_prefix(url)) {
    errnos.push_back(e_http_prefix);
    return false;
  }

  if(!extract_host_and_port(url, host, port, errnos)) {
    return false;
  }

  // TODO: DNS host verification

  path = url;
  if(PATH_REQUIRED) {
    if(path.length() <= 0) {
      errnos.push_back(e_path);
      return false;
    }
  }

  return true;
}

/* extracts each header line from 'msg' into an element in 'headers' and returns
 * true if headers are present; otherwise returns false */
bool extract_headers(std::string msg, std::vector<std::string> &headers) {
  std::string eol_delim = "\r\n";
  std::size_t pos = 0;

  if(msg.empty()) { return false; }

  while(!msg.empty()) {
    pos = msg.find(eol_delim);
    if(pos != std::string::npos && pos > 0) {
      headers.push_back(msg.substr(0, pos));
    }
    msg.erase(0, pos + eol_delim.size());
  }
  return true;
}

/* extracts three space-delimited elements from the client request line: METHOD,
 * URL, and HTTP VERSION, assigns elements to respective variables, and returns
 * true; otherwise, returns false if there are more/fewer than three elements */
bool extract_request_elements(std::string req, std::string& method,
    std::string& url, std::string& http_vers, std::vector<Error>& errnos) {
  trim(req);
  // extract each space-delimited element into a vector entry
  std::istringstream iss(req);
  std::istream_iterator<std::string> beg(iss), end;
  std::vector<std::string> elements(beg, end);

  // check that there are exactly three elements
  int num_elements = elements.size();
  if(num_elements != 3) { return false; }

  // assign each element to its respective variable
  method = elements.at(0);
  url = elements.at(1);
  http_vers = elements.at(2);

  return true;
}

/* if PATH_REQUIRED == false : extracts and removes the host and port from a url
 *    formatted as [host(:port)] and returns true; otherwise returns false if no
 *    'host(:port)' is found
 * if PATH_REQUIRED == true : extracts and removes the host and port from a url
 *    formatted as [host(:port)/path...] and returns true; otherwise returns
 *    false if no 'host(:port)/' is found */
bool extract_host_and_port(std::string& url, std::string& host,
  std::string& port, std::vector<Error>& errnos) {
  std::string host_delim = "/";
  std::size_t pos = url.find(host_delim);

  if(pos == std::string::npos && PATH_REQUIRED) {
    errnos.push_back(e_path);
    return false;   // no '/' in 'host(:port)/...' found
  }

  host = url.substr(0, pos);
  if(pos == std::string::npos) {
    url.erase(0, std::string::npos);  // no path included, url empty
  }
  else {
    url.erase(0, pos + host_delim.size());  // remaining url is path
  }
  trim(host);
  if(host.length() <= 0 ) {
    errnos.push_back(e_host);
    return false; // no host found
  }

  return extract_and_verify_port(host, port, errnos);
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

/* accepts a host (and port) formatted as 'host' or 'host:port', respectively,
 * assigns each to the appropriate variable, assigns default port number if none
 * is present, and returns true; otherwise returns false for an invalid port */
bool extract_and_verify_port(std::string& host, std::string& port,
    std::vector<Error>& errnos) {
  std::string port_delim = ":";
  std::string default_port = DEFAULT_PORT;
  std::size_t pos = host.find(port_delim);

  // if no port included, assign default port
  if(pos == std::string::npos) {
    port = default_port;
    return true;
  }

  // if port included, divide from host
  port = host.substr(pos + port_delim.size());
  host = host.substr(0, pos);

  if(!verify_port(port)) {
    errnos.push_back(e_port);
    return false;
  }

  return true;
}

/* verifies that method is accepted (only GET in this assignment) and formatted
 * in uppercase; otherwise returns false on mismatch */
bool verify_method(std::string& method) {
  std::string valid_method = VALID_METHOD;
  trim(method);
  return is_match_caseins(valid_method, method);
}

/* verifies that the HTTP version is accepted (only HTTP/1.0 in this assignment)
 * and formatted in uppercase; otherwise returns false on mismatch */
bool verify_http_vers(std::string& http_vers) {
  std::string valid_http_vers = VALID_HTTP_VERS;
  trim(http_vers);
  return is_match_caseins(valid_http_vers, http_vers);
}

/* returns true if each header has valid format [name: value] where name has no
 * embedded whitespaces; otherwise returns false
 * Note: formats header to remove any whitespace between the name and ":" and
 * removes any duplicate 'Host' or 'Connection' headers */
bool verify_headers(std::vector<std::string> &headers) {
  std::size_t pos = 0;
  std::string name_delim = ":";
  std::string whitespace = " \r\n\t";
  std::string def_host = "Host", def_conn = "Connection";
  std::vector<int> dup_headers_found;

  std::string orig_header, name, value;

  int num_elem = headers.size();
  for(int i = 0; i < num_elem; ++i) {

    orig_header = headers.at(i);
    pos = orig_header.find(name_delim);
    if(pos == std::string::npos) { return false; }  // missing ':'

    name = orig_header.substr(0, pos);
    value = orig_header.substr(pos);  // includes ":"

    trim(name);
    pos = name.find_first_of(whitespace);
    if(pos != std::string::npos) { return false; }  // name has internal spaces
    pos = value.find_first_not_of(whitespace);
    if(pos == std::string::npos) { return false; }  // header has no value

    headers.at(i) = name + value;

    // track indices of headers with names: 'Host' and/or 'Connection'
    if(is_match_caseins(def_host, name) || is_match_caseins(def_conn, name)) {
      dup_headers_found.push_back(i);
    }
  }

  // remove duplicate 'Host' (extracted from url) and 'Connection' (defaults to
  // close) headers -> these are automatically added to the web server request
  for(std::vector<int>::reverse_iterator rit = dup_headers_found.rbegin();
      rit != dup_headers_found.rend(); ++rit) {
    headers.erase(headers.begin() + *rit);
    // headers.erase(*rit);
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
