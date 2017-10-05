---------------------------------------------
README: A Simple Client Server Warmup Project
---------------------------------------------
AUTHOR: Adrienne Grieco
DATE: 10/05/2017
INCLUDED: fingerserver.cpp
          fingerclient.cpp
          Makefile
          README.txt

CONTENTS
--------
    1. Purpose
    3. Basic Assumptions and Information
    2. Compiling and Running the Program

1. PURPOSE
----------
This project uses socket programming to create TCP connections between clients
and a multi-client server. Using this connection, clients will connect to the
server, send the server a username, and the server will respond with the finger
service data for that username.

2. BASIC ASSUMPTIONS AND INFORMATION
------------------------------------
SERVER:
Port Number:  automatically set to *10042*
# Connections:  maximum of *10* client connections allowed simultaneously
Client Request: server receives a *username* from a client which may not exceed
                100 characters; otherwise client request is rejected


3. COMPILING AND RUNNING THE PROGRAM
------------------------------------
Use "make" to compile the client and server into executables:
  fingerclient and fingerserver
Note: "make clean" will remove these executables.

Start the SERVER on Port Number 10042 (automatically selected) by typing into
the command line:
    ./fingerserver

After the server is running, in a new console (or multiple new consoles), run
the CLIENT program(s) by typing into the command line:
    ./fingerclient <username@hostname:server_port>
          example:  "./fingerclient griecoa1@cs1.seattleu.edu:10042"
          Note: the server's Port Number will always be set to 10042.
