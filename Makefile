CC = gcc
CFLAGS = -Wall -Wextra -g

sshell: sshell.o argvList.o
	$(CC) $(CFLAGS) -o sshell sshell.o argvList.o

sshell.o: sshell.c argvList.h
	$(CC) $(CFLAGS) -c sshell.c  

argvList.o: argvList.c argvList.h
	$(CC) $(CFLAGS) -c argvList.c 

clean:
	rm -f shell *.o