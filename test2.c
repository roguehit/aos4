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
rvm = rvm_init("rvm_segments");
char* segment = (char*) rvm_map(rvm,"testsegment",1000);

rvm_unmap(rvm,segment);

char* newsegment = (char*) rvm_map(rvm,"testsegment",2000);
/*XXX This needs to be in the cleanup function*/
printf("Ok\n");
return 0;
}
