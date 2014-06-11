XmodemServer
============

Xmodem Server Implementation

Before compiling, in the Makefile, change the port number to the applicable number for your machine.
To compile, run "make all" then to run the server run xmodemserver.
This will start up a xmodemserver that will connect to multiple clients (with the proper protocol) and receive 
files from its clients simultaneously into a new folder in the working directory.
