all: sender receiver

sender: sender.o
	gcc -o sender -g -pthread sender.o
	
sender.o: sender.c
	gcc -g -c -Wall sender.c
	
receiver: receiver.o
	gcc -o receiver -g -pthread receiver.o

receiver.o: receiver.c
	gcc -g -c -Wall receiver.c	

clean:
	rm -f *.o sender receiver
