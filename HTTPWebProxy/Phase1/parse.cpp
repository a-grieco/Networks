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

bool parse_client_msg(std::string msg, std::string& host, std::string& path,
    std::string& port, std::vector<std::string> &headers) {
  bool is_valid = true;
  if(DEBUG_MODE) { printf("%s\n", "parsing client message"); }
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
    if(DEBUG_MODE) {
      if(is_valid) { printf("headers valid\n");}
      else { printf("headers invalid!\n");}
    }
  }
  return is_valid;
}

/* parses request line into host, path, and port number and returns true;
 * otherwise, returns false for invalid formatting and/or bad DNS */
bool parse_request_line(std::string req, std::string& host, std::string& path,
    std::string &port) {
  bool is_valid = true;
  if(DEBUG_MODE) { printf("parsing request line\n"); }
  // line should contain 3 parts <METHOD> <URL> <HTTP VERSION>
  std::string method, url, http_vers;
  is_valid = extract_request_elements(req, method, url, http_vers);
  if(DEBUG_MODE) {
    if(is_valid) {
      printf("method: %s, url: %s, http_vers: %s\n", method.c_str(),
        url.c_str(), http_vers.c_str());
    }
    else {
      printf("extract_request_elements returned false\n");
    }
  }
  // if request line is valid, extract host, path, and port number
  if(is_valid) {
    is_valid = (verify_method(method) && verify_http_vers(http_vers) &&
      verify_url(url, host, path, port));
    if(DEBUG_MODE) {
      if(is_valid) {
        printf("method: %s, http_vers: %s, url: %s\n", method.c_str(),
          http_vers.c_str(), url.c_str());
        printf("host: %s, path: %s, port: %s\n", host.c_str(), path.c_str(),
          port.c_str());
      }
      else { printf("invalid method, http, and/or url\n"); }
    }
  }

  return is_valid;
  // TODO: check DNS for valid path
}

/* parses headers into a vector of strings (each index as <name: value>) and
 * returns true; otherwise returns false for invalid formatting */
bool parse_headers(std::string msg, std::vector<std::string> &headers) {
  if(DEBUG_MODE) { printf("parsing headers...\n"); }
  // TODO: break into two methods, parse then validate
  // parse each header by newline
  std::string eol_delim = "\r\n";
  std::size_t pos = 0;
  while(!msg.empty()) {
    pos = msg.find(eol_delim);
    if(pos != std::string::npos && pos > 0) {
      headers.push_back(msg.substr(0, pos));
    }
    msg.erase(0, pos + eol_delim.size());
  }


  // check each line for [header: value] format
  std::string name_delim = ":";
  std::string whitespace = " \r\n\t";
  std::string curr_header, header_name;
  int num_elem = headers.size();
  for(int i = 0; i < num_elem; ++i) {
    curr_header = headers.at(i);
    pos = curr_header.find(name_delim);
    if(pos == std::string::npos) { return false; } // invalid format
    header_name = curr_header.substr(0, pos);
    pos = header_name.find_first_of(whitespace);
    if(pos != std::string::npos) { return false; } // invalid format
  }

  if(DEBUG_MODE) {
    printf("headers:\n[");
    for(std::vector<std::string>::iterator i = headers.begin();
        i != headers.end(); ++i) {
      printf("%s\n", (*i).c_str());
    }
    printf("]\n");
  }
  return true;
}

/* extracts three space-delimited elements from the client request line: METHOD,
 * URL, and HTTP VERSION, assigns to respective variables, and returns true;
 * otherwise, returns false if there are more/fewer than three elements.
 * Note: does not ensure validity of variables */
bool extract_request_elements(std::string req, std::string& method,
    std::string& url, std::string& http_vers) {
  trim(req);
  // convert each space-delimited element into a vector entry
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

/* verifies that method is accepted (only GET in this assignment) and formatted
 * in uppercase; otherwise returns false on mismatch */
bool verify_method(std::string& method) {
  std::string valid_method = "GET";
  trim(method);
  if(DEBUG_MODE) {
    if(is_match_caseins(valid_method, method)) {
      printf("verify_method: true\n");
      return true;
    }
    else {
      printf("verify_method: false\n");
      return false;
    }
  }
  return is_match_caseins(valid_method, method);
}

/* verifies that the HTTP version is accepted (only HTTP/1.0 in this assignment)
 * and formatted in uppercase; otherwise returns false on mismatch */
bool verify_http_vers(std::string& http_vers) {
  std::string valid_http_vers = "HTTP/1.0";
  trim(http_vers);
  if(DEBUG_MODE) {
    if(is_match_caseins(valid_http_vers, http_vers)) {
      printf("verify_http_vers: true\n");
      return true;
    }
    else {
      printf("verify_http_vers: false\n");
      return false;
    }
  }
  return is_match_caseins(valid_http_vers, http_vers);
}

/* verifies that url is acceptable and parses host, path, and port; otherwise
 * returns false */
bool verify_url(std::string url, std::string& host, std::string& path,
  std::string& port) {
    // TODO: these s/b 3 helper functions -> ...and parsing s/b its own class
    std::string first_delim = "//";
    std::string second_delim = "/";
    std::string third_delim = ":";
    std::string default_port = "80";
    std::size_t pos = 0;

    trim(url);

    // verify correct http:// prefix
    std::string valid_http_prefix = "http:";
    std::string http_prefix;
    pos = url.find(first_delim);
    if(pos == std::string::npos) { return false; }
    http_prefix = url.substr(0, pos);
    url.erase(0, pos + first_delim.size());
    if(!is_match_caseins(valid_http_prefix, http_prefix)) { return false; }

    // extract host (including port # if present)
    pos = url.find(second_delim);
    // TODO: no path specified - check: IS THIS ALLOWABLE?
    // at present - this will reject a url without a path (term: "/")
    if(pos == std::string::npos) { return false; }
    host = url.substr(0, pos);
    url.erase(0, pos + second_delim.size());  //
    // extract port # if present
    pos = host.find(third_delim);
    if(pos == std::string::npos) {
      port = default_port;
    }
    else {
      port = host.substr(pos + third_delim.size());
      host = host.substr(0, pos);
    }
    // TODO: check DNS for valid host
    // TODO: verify that port is valid number

    // extract path
    path = url;
    // TODO: check formatting? terms with .html? etc.? does this need to exist? - check length.

    if(DEBUG_MODE) {
      printf("in verify_url\n");
      printf("   host: %s\n", host.c_str());
      printf("   port: %s\n", port.c_str());
      if(!path.empty()) { printf("   path: %s\n", path.c_str()); }
    }

    return true;
}

/* if two strings match (case insensitive); entry is assigned prefered
 * formatting and function returns true; otherwise returns false on mismatch */
 bool is_match_caseins(std::string valid, std::string& entry) {
   if(entry.length() == valid.length()) {
     for(int i = 0; i < valid.length(); ++i) {
       if(toupper(entry[i]) != toupper(valid[i])) {
         return false;
       }
     }
     entry = valid;
     return true;
   }
   else { return false; }
 }

 /* trims leading and trailing whitespace from a string; allows correction of
  * excess whitespace in a line of the client request */
 std::string& trim(std::string& str) {
   std::string whitespace = " \r\n\t";
 	str.erase(0, str.find_first_not_of(whitespace));
 	str.erase(str.find_last_not_of(whitespace) + 1);
 	return str;
 }
