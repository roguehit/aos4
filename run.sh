#! /bin/bash
rm out
for i in basic.c abort.c multi-abort.c truncate.c multi.c test1.c test2.c
do
	rm -r rvm_segments
	make testprogram DEFAULT=$i
	./test 
	echo -------------------------------------------
done
