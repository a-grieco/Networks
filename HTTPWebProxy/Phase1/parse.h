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

enum Error { e_method, e_url, e_http_vers, e_http_prefix, e_host, e_dns,
  e_path, e_port, e_headers };

bool get_parsed_data(std::string client_msg, std::string& webserv_host,
  std::string& webserv_port, std::string& data);

void generate_webserver_request(std::string& data, std::string& host,
  std::string& path, std::vector<std::string>& headers);
void generate_client_error_msg(std::string& data, std::vector<Error>& errnos);
void include_error_detail(std::string& data, std::vector<Error>& errnos);

bool parse_client_msg(std::string msg, std::string& host, std::string& path,
  std::string& port, std::vector<std::string>& headers,
  std::vector<Error>& errnos);
bool parse_request_line(std::string req, std::string& host, std::string& path,
  std::string &port, std::vector<Error>& errnos);
bool parse_headers(std::string msg, std::vector<std::string> &headers,
  std::vector<Error>& errnos);
bool parse_url(std::string url, std::string& host, std::string& path,
  std::string& port, std::vector<Error>& errnos);
bool extract_headers(std::string msg, std::vector<std::string> &headers);
bool extract_request_elements(std::string req, std::string& method,
  std::string& url, std::string& http_vers, std::vector<Error>& errnos);
bool extract_host_and_port(std::string& url, std::string& host,
  std::string& port, std::vector<Error>& errnos);
bool extract_and_verify_http_prefix(std::string& url);
bool extract_and_verify_port(std::string& host, std::string& port,
  std::vector<Error>& errnos);
bool verify_method(std::string& method);
bool verify_http_vers(std::string& http_vers);
bool verify_headers(std::vector<std::string> &headers);
bool verify_port(std::string& port);
bool is_match_caseins(std::string valid, std::string& entry);
void trim(std::string& str);

#endif // PARSE_H
