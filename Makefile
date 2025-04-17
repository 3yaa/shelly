CC = gcc
CFLAGS = -Wall -Wextra -g

sshell: sshell.o
	$(CC) $(CFLAGS) -o sshell sshell.o

sshell.o: sshell.c
	$(CC) $(CFLAGS) -c sshell.c 

clean:
	rm -f shell *.o