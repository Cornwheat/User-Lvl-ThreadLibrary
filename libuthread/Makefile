cc = gcc
CFLAGS = -Wall -Werror
lib := libuthread.a

all: $(lib)

clean:
	rm -f *.o
	rm -f libuthread


tps.o: tps.c
	$(cc) -g -c tps.c -o tps.o


sem.o: sem.c
	$(cc) -g -c sem.c -o sem.o


queue.o: 
	$(cc) -g -c queue.o


thread.o:
	$(cc) -g -c thread.o


libuthread.a: queue.o thread.o sem.o tps.o
	ar rcs libuthread.a sem.o queue.o thread.o tps.o
