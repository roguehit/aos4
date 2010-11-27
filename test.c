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
int main (int argc, char** argv)
{
rvm_t rvm;
rvm = rvm_init("testfolder");
char* segment = (char*) rvm_map(rvm,"testsegment",1000);
char* seg2 = (char*) rvm_map(rvm,"testsegment",1200);
if(seg2 == (rvm_t)-1){
printf("Mapping Failed\n");
}

printf("%s\n",rvm->dir);
sprintf(segment,"%s\n","Testing....");

fprintf(rvm->log,"Testing\n");

printf("Just Wrote %s",segment);
//printf("Segment Name:%s\n", rvm->segment[0]->name);


//rvm_destroy(rvm,"testsegment");

//rvm_commit_trans((void*)segment,rvm->segment[0]->fd,rvm->segment[0]->size);


rvm_unmap(rvm,segment);
/*XXX This needs to be in the cleanup function*/
close(rvm->segment[0]->fd);
free(rvm->segment[0]);

fclose(rvm->log);
free(rvm);

return 0;
}
