/* parse.h */
// used by proxy for parsing client HTTP requests

#ifndef PARSE_H
#define PARSE_H

const int SMALL_BUFFERSIZE = 1000;
const int BUFFERSIZE = 3000;

enum Parse_Error { e_req_line, e_method, e_url, e_http_vers, e_http_prefix,
  e_host, e_dns, e_path, e_port, e_headers, e_name_ws, e_header_val };

bool get_parsed_data(int req_size, int header_size,
  char (&req_buf)[SMALL_BUFFERSIZE], char(&header_buf)[BUFFERSIZE],
  std::string& webserv_host, std::string& webserv_port, std::string& data);

void generate_webserver_request(std::string& data, std::string& host,
  std::string& path, std::string& port);
void get_parse_error_msg(std::string& data, Parse_Error& err);

bool parse_request_line(std::string& req, std::string& host, std::string& path,
  std::string &port, Parse_Error& err);
bool parse_headers(char(&header_buf)[BUFFERSIZE], int header_size,
  Parse_Error& err);
bool parse_url(std::string url, std::string& host, std::string& path,
  std::string& port, Parse_Error& err);
bool extract_headers(std::string h_str, std::vector<std::string> &headers,
  std::vector<int> size_so_far);
bool extract_request_elements(std::string req, std::string& method,
  std::string& url, std::string& http_vers, Parse_Error& err);
bool extract_host_and_port(std::string& url, std::string& host,
  std::string& port, Parse_Error& err);
bool extract_and_verify_http_prefix(std::string& url);
bool extract_and_verify_port(std::string& host, std::string& port,
  Parse_Error& err);
bool verify_method(std::string& method);
bool verify_http_vers(std::string& http_vers);
bool verify_headers(std::vector<std::string> &headers, Parse_Error& err,
  std::vector<int> &size_so_far, char(&header_buf)[BUFFERSIZE],
  int header_size);
bool verify_port(std::string& port);
bool is_match_caseins(std::string valid, std::string& entry);
void trim(std::string& str);

#endif // PARSE_H
