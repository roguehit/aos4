CFLAGS  = -g -Wall 
DEFAULT = basic.c

.phony: clean testprogram

librvm.a: rvm.o
	ar rcs librvm.a rvm.o

rvm.o: rvm.c 
	gcc $(CFLAGS) -c rvm.c

clean:
	rm rvm.o test 

testprogram:
	gcc $(CFLAGS) $(DEFAULT) -o test -L. -lrvm
