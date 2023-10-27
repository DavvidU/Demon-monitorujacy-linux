demon: monitoruj.o
	cc -o demon monitoruj.o -lcrypto

monitoruj.o: monitoruj.c
	cc -c monitoruj.c -lcrypto

clean:
	rm demon monitoruj.o
