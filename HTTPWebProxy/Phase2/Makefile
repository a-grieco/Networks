CFLAGS = -O
CC = g++
SRC = proxy.cpp parse.cpp
OBJ = $(SRC:.cpp = .o)

Proxy: $(OBJ)
	$(CC) $(CFLAGS) -o proxy $(OBJ)

clean:
	rm -f core *.o proxy
