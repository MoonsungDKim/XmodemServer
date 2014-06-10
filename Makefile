CC = gcc
PORT=52016
CFLAGS = -DPORT=$(PORT) -g -ggdb -Wall

all: xmodemserver 

xmodemserver: xmodemserver.o wrapsock.o crc16.o
	${CC} ${CFLAGS} -o $@ xmodemserver.o wrapsock.o crc16.o

xmodemserver.o: xmodemserver.c xmodemserver.h
	${CC} ${CFLAGS} -c $<

wrapsock.o: wrapsock.c wrapsock.h
	${CC} ${CFLAGS} -c $<

crc16.o: crc16.c crc16.h
	${CC} ${CFLAGS} -c $<


clean:
	rm *.o
