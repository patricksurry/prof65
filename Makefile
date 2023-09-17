all: prof65

prof65: prof65.c
	gcc -I../MyLittle6502 -o prof65 -lreadline prof65.c

clean:
	rm -f *.o prof65
