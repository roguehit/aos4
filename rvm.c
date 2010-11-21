#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <assert.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <signal.h>

#include "rvm.h"
#define _GNU_SOURCE

static void block_signal();
static void unblock_signal();

rvm_t rvm_init(const char *directory){

	rvm_t RVM = (struct __rvm_t *) malloc (sizeof (struct __rvm_t));
	char filename[500];
	RVM->segNo = 0;
	/*Storing the Backup Directory path*/
	sprintf(RVM->dir,"%s/%s",getenv("PWD"),directory);
	printf("Dir:%s\n",RVM->dir);

	/*Creating Backup Directory*/
	if(mkdir(RVM->dir,S_IRUSR | S_IWUSR | S_IXUSR)){
		if(errno == EEXIST){
			fprintf(stderr,"Directory Already Exits\n");
		}
		else
			fprintf(stderr,"Creation of Directory \
			failed : %d\n",errno);
		/*Handle Error from MKDIR*/
	}

	sprintf(filename,"%s/test.log",RVM->dir); 
	printf("Creating file %s\n",filename);

	if( (RVM->log = fopen (filename,"aw")) == NULL ){
		fprintf(stderr,"Init Failed, Shutting Down\n");
		exit(EXIT_FAILURE);
	}

	return RVM;
}
int lockfile(int fd)
{
	block_signal();
	int retval = flock(fd,LOCK_EX);
	unblock_signal();
	return retval;
}

int unlockfile(int fd)
{
	return flock(fd,LOCK_UN);
}

int filesize(int fd)
{
	struct stat info;

	if( fstat(fd,&info) == -1){
	fprintf(stderr,"File Suddenly Disappeared\n");
	//exit(EXIT_FAILURE);
	return -1;
	}
 	printf("Hardlinks to file : %d",info.st_nlink);

	/*
	*  Making Sure the file is not special
	*  and does not have more than 1 hardlink
	*/

	assert(info.st_nlink == 1);

	return info.st_size;
}

void *rvm_map(rvm_t rvm, const char *segname, int size_to_create){

	char dirfile[500];
	memSeg *seg;

	sprintf(dirfile,"%s.dir",rvm->dir);
	int dirfp = open (dirfile,O_RDWR | O_CREAT , S_IRWXU | S_IRWXG);

	if(dirfp == -1){
		fprintf(stderr,"Could not create dummy directory file to lock\n");
	}

	if(lockfile(dirfp) == -1){
		fprintf(stderr,"Lock Failed\n");
		exit(EXIT_FAILURE);
	}

        seg = (memSeg*) malloc(sizeof(memSeg));

	/*Open a segment*/
	seg->fd = open(segname,O_RDWR | O_CREAT | O_EXCL,S_IRWXU | S_IRWXG);
	/*BrandNew Segment File*/
	if( seg->fd > 0 ){
		seg->size = size_to_create;
		seg->address = mmap(NULL,size_to_create,
				PROT_READ | PROT_WRITE,
				MAP_PRIVATE,
				seg->fd,0);

		//seg->fd = lseek(seg->fd,SEEK_SET,0);
	}else { 
		/*Backup Segment is already present*/
	
		seg->fd = open(segname,O_RDWR);
		int size = 0;
		if( (size = filesize(seg->fd)) == -1 ){
		fprintf(stderr,"File Disappeared ? \n");
		unlockfile(dirfp);
		exit(EXIT_FAILURE);
		} 

		/*Shrinking of Segment not allowed*/
		assert(size <= size_to_create);

		if( size == size_to_create){
			seg->size = size_to_create;
			seg->address = mmap(NULL,size_to_create,
					PROT_READ | PROT_WRITE,
					MAP_PRIVATE,
					seg->fd,0);
			
		}else if(size < size_to_create){
			seg->address = mmap(NULL,size,
					PROT_READ | PROT_WRITE,
					MAP_PRIVATE,
					seg->fd,0);
			/*Remap Here*/
			/*XXX mremap does not work ??
			seg->address = mremap(seg->address,
					(size_t)size,
					(size_t)size_to_create,
					MREMAP_MAYMOVE);
			*/
			seg->address = realloc(seg->address,
					       size_to_create);
		}

	}
	unlockfile(dirfp);
	close(dirfp);

	rvm->segment[rvm->segNo++] = seg;

	return seg->address;
}
static void block_signal()
{
        sigset_t set;

        /* Block the signal */
        sigfillset(&set);
        //sigaddset(&set, signo);
        sigprocmask(SIG_BLOCK, &set, NULL);

        return;
}

static void unblock_signal()
{
        sigset_t set;

        /* Unblock the signal */
        sigfillset(&set);
        //sigaddset(&set, signo);
        sigprocmask(SIG_UNBLOCK, &set, NULL);

        return;
}

