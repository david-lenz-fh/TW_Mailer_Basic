CC = gcc
CFLAGS = -Wall -Wextra

all: twmailer-server twmailer-client

twmailer-server: twmailer-server.o
	$(CC) $(CFLAGS) twmailer-server.o -o twmailer-server
twmailer-server.o: twmailer-server.c twmailer-server.h
	$(CC) $(CFLAGS) -c twmailer-server.c -o twmailer-server.o
twmailer-client: twmailer-client.o
	$(CC) $(CFLAGS) twmailer-client.o -o twmailer-client
twmailer-client.o: twmailer-client.c
	$(CC) $(CFLAGS) -c twmailer-client.c -o twmailer-client.o
clean: 
	rm -f $(TARGET) twmailer-server.o
	rm -f $(TARGET) twmailer-server
	rm -f $(TARGET) twmailer-client.o
	rm -f $(TARGET) twmailer-client

