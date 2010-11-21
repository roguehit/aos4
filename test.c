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

printf("%s\n",rvm->dir);

fprintf(rvm->log,"Testing\n");

/*XXX This needs to be in the cleanup function*/
fclose(rvm->log);
free(rvm);

return 0;
}
