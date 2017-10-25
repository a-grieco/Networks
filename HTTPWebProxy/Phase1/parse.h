/* parse.h */
// used by proxy for parsing client HTTP requests

#ifndef PARSE_H
#define PARSE_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <iostream>
#include <errno.h>
#include <vector>
#include <sstream>
#include <iterator>

bool parse_client_msg(std::string msg, std::string& host, std::string& path,
    std::string& port, std::vector<std::string> &headers);
bool parse_request_line(std::string req, std::string& host, std::string& path,
    std::string &port);
bool parse_headers(std::string msg, std::vector<std::string> &headers);
bool extract_request_elements(std::string req, std::string& method,
    std::string& url, std::string& http_vers);
bool verify_method(std::string& method);
bool verify_http_vers(std::string& http_vers);
bool verify_url(std::string url, std::string& host, std::string& path,
  std::string& port);
bool is_match_caseins(std::string valid, std::string& entry);
std::string& trim(std::string& str);

#endif // PARSE_H
