CFLAGS = -O3
CC = g++ -g -std=c++11
SRC = proxy.cpp parse.cpp
OBJ = $(SRC:.cpp = .o)

Proxy: $(OBJ)
	$(CC) $(CFLAGS) -pthread -o proxy $(OBJ)

clean:
	rm -f core *.o proxy
