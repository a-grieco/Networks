// GET https://www.yahoo.com/beauty/deaf-baby-hears-mom-say-love-first-time-gets-emotional-will-194027342.html HTTP/1.0

#include <iostream>
#include <string.h>
#include <vector>

int main(int argc, char *argv[])
{
	std::vector<std::string> proxy_input;
	std::vector<std::string> request_line_tokens;
	std::string input = "temp";
	std::string request_line = "";
	std::string delimiter = " ";
	
	// taking in multiple input until blank line
	while(!input.empty())
	{
		getline(std::cin, input);
		proxy_input.push_back(input);
	}
	
	request_line = proxy_input[0]; // request line is always first
	size_t pos = 0;
	
	// split header line
	while ((pos = request_line.find(delimiter)) != std::string::npos)
	{
		request_line_tokens.push_back(request_line.substr(0, pos));
		request_line.erase(0, pos + delimiter.length());
	}
	// needed for including the back of the request line
	// because a delimiter will not be found anymore
	request_line_tokens.push_back(request_line);
	
	unsigned int counter = 0;
	
	// just testing
	std::cout << std::endl << std::endl << "PRINTING FOR TEST" << std::endl << std::endl;
	
	while (counter < request_line_tokens.size())
	{
		std::cout << request_line_tokens[counter] << std::endl;
		
		counter++;
	}
	
	return 0;
}