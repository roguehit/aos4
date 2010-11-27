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
#define TEST_STRING "hello, world"

int main (int argc, char** argv)
{
	rvm_t rvm;
	trans_t trans;
	rvm = rvm_init("testfolder");
	char* segment[0], *testseg;
	segment[0] = (char*) rvm_map(rvm,"testsegment",1000);

	testseg    = (char*) rvm_map(rvm,"testsegment1",1000);
	if(testseg == (rvm_t) -1 ){
		printf("Mapping Failed ?\n");
	}else
		printf("testsegment at %08x -- testsegment1 at %08x\n",(int)(char*)(segment[0]),(int)(char*)(testseg));
	rvm_unmap(rvm,testseg);

	/* Begin tRansaction */
	trans = rvm_begin_trans(rvm, 1, (void **) segment);

	printf("%s\n",rvm->dir);
	sprintf(segment[0],"%s\n","Testing....");

	printf("Just Wrote %s",segment[0]);
	//printf("Segment Name:%s\n", rvm->segment[0]->name);
	/*Update Transaction */
	rvm_about_to_modify(trans, segment[0], 0, 10);
	sprintf(segment[0], TEST_STRING);


	rvm_unmap(rvm,segment[0]);
	//rvm_destroy(rvm,"testsegment");

	/*XXX This needs to be in the cleanup function*/
	close(rvm->segment[0]->fd);
	free(rvm->segment[0]);

	fclose(rvm->log);
	free(rvm);

	return 0;
}
