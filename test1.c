#include <stdio.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <assert.h>

#include "rvm.h"
#define GOOD_STRING "hello, world"
#define BAD_STRING "Georgia"

void proc1()
{
	rvm_t rvm;
	trans_t trans;
	rvm = rvm_init("rvm_segments");
	char* segs[0];
	segs[0] = (char*) rvm_map(rvm,"testsegment",1000);

	trans = rvm_begin_trans(rvm, 1, (void **) segs);     

	rvm_about_to_modify(trans, segs[0], 0, 40);
	sprintf(segs[0], GOOD_STRING);

	rvm_commit_trans(trans);

	rvm_about_to_modify(trans, segs[0], 0, 40);
	sprintf(segs[0], BAD_STRING);

	abort();
	return;
}

void proc2()
{
	char* segs[1];
	rvm_t rvm;
	rvm = rvm_init("rvm_segments");

	segs[0] = (char *) rvm_map(rvm, "testsegment", 1000);
	printf("String : %s\n",segs[0]);
	if(strcmp(segs[0], GOOD_STRING)) {
		printf("ERROR: Good String not present\n");
		exit(2);
	}
	printf("OK\n");
	exit(0);

}

int main(int argc, char **argv)
{
     int pid;
	rvm_verbose(0);
     pid = fork();
     if(pid < 0) {
	  perror("fork");
	  exit(2);
     }
     if(pid == 0) {
	  proc1();
	  exit(0);
     }

     waitpid(pid, NULL, 0);
    proc2();

     return 0;
}
