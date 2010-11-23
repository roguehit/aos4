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

printf("%s\n",rvm->dir);
sprintf(segment,"%s\n","Testing....");

fprintf(rvm->log,"Testing\n");

printf("Just Wrote %s",segment);
//printf("Segment Name:%s\n", rvm->segment[0]->name);
/*XXX This needs to be in the cleanup function*/


rvm_unmap(rvm,segment);
rvm_destroy(rvm,"testsegment");

close(rvm->segment[0]->fd);
free(rvm->segment[0]);

fclose(rvm->log);
free(rvm);

return 0;
}
