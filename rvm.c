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
#include <errno.h>
#include "rvm.h"
#define _GNU_SOURCE

static void block_signal();
static void unblock_signal();
static void fill_region(void* address , int size);
static int index_from_address(rvm_t rvm, void* segbase);

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
	/*
	*Flock is blocking & Signals can screw up
	*hence block all signals.	
	*/
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
//	printf("Hardlinks to file : %d\n",info.st_nlink);

	/*
	*Making Sure the file is not special
	*and does not have more than 1 hardlink
	*/
	assert(info.st_nlink == 1);

	return info.st_size;
}

static void fill_region(void *address,int size)
{
	int i;
	char *temp = (char*)address;
	for(i = 0; i < size;i++)
	{
	*(temp+i) = 1;
	}

return;
}
void *rvm_map(rvm_t rvm, const char *segname, int size_to_create){

	char dirfile[500];
	memSeg *seg;
	char filepath[500];
	sprintf(dirfile,"%s/test.dir",rvm->dir);
	int dirfp = open (dirfile,O_RDWR | O_CREAT , S_IRWXU | S_IRWXG);

	if(dirfp == -1){
		fprintf(stderr,"Could not create dummy directory file to lock\n");
	}

	if(lockfile(dirfp) == -1){
		fprintf(stderr,"Lock Failed\n");
		exit(EXIT_FAILURE);
	}

        seg = (memSeg*) malloc(sizeof(memSeg));
//	printf("Here\n");
	strcpy(seg->name,segname);

	sprintf(filepath,"%s/%s",rvm->dir,segname);
	strcpy(seg->path,filepath);
	/*Open a segment*/
	seg->fd = open(filepath,O_RDWR | O_CREAT | O_EXCL,S_IRWXU | S_IRWXG);
	/*BrandNew Segment File*/
	if( seg->fd >= 0 ){
//	printf("Here\n");
		seg->size = size_to_create;
		write(seg->fd,(void*)"testing",1);	
	
		seg->address = mmap(NULL,size_to_create,
				PROT_READ | PROT_WRITE,
				MAP_PRIVATE,
				seg->fd,0);

		seg->fd = lseek(seg->fd,SEEK_SET,0);

	printf("Here\n");
		if(seg->address == (void*)-1){
			fprintf(stderr,"mmap failed:%s, Line %d\n",strerror(errno),__LINE__);
			unlockfile(dirfp);
			close(dirfp);
			return (rvm_t)-1;
		}else{
		//printf("%d",(size_t)size_to_create);
		//memset(seg->address,1,(size_t)size_to_create);
		fill_region(seg->address,size_to_create);
		}
		//seg->fd = lseek(seg->fd,SEEK_SET,0);
	}else { 
		/*Backup Segment is already present*/

		seg->fd = open(filepath,O_RDWR);
		int size = 0;
		if( (size = filesize(seg->fd)) == -1 ){
			fprintf(stderr,"File Disappeared ? \n");
			unlockfile(dirfp);
			close(dirfp);
			exit(EXIT_FAILURE);
		}
		printf("Size of original segment:%d, Requested:%d\n",size,size_to_create);
		/*Shrinking of Segment not allowed*/
		assert(size <= size_to_create);

		if( size == size_to_create){
			seg->size = size_to_create;
			seg->address = mmap(NULL,size_to_create,
					PROT_READ | PROT_WRITE,
					MAP_PRIVATE,
					seg->fd,0);

			if(seg->address == (void*)-1){
				fprintf(stderr,"mmap failed:%s Line %d\n",strerror(errno),__LINE__);
				unlockfile(dirfp);
				close(dirfp);
				return (rvm_t)-1;
			}
		}else if(size < size_to_create){
			seg->address = mmap(NULL,size,
					PROT_READ | PROT_WRITE,
					MAP_PRIVATE,
					seg->fd,0);
			/*Remap Here*/
			if(seg->address == (void*)-1){
				fprintf(stderr,"mmap failed:%s Line %d\n",strerror(errno),__LINE__);
				unlockfile(dirfp);
				close(dirfp);
				return (rvm_t)-1;
			}
			/*XXX mremap does not work ??
			  seg->address = mremap(seg->address,
			  (size_t)size,
			  (size_t)size_to_create,
			  MREMAP_MAYMOVE);
			 */
			seg->address = realloc(seg->address,
					size_to_create);
			if(seg->address == (void*)-1){
				fprintf(stderr,"Realloc failed:%s Line %d\n",strerror(errno),__LINE__);
				unlockfile(dirfp);
				close(dirfp);
				return (rvm_t)-1;
			}
		}

	}
	unlockfile(dirfp);
	close(dirfp);

	rvm->segment[rvm->segNo] = seg;

	return rvm->segment[(rvm->segNo)++]->address;
}

static int index_from_address(rvm_t rvm, void* segbase)
{
	int i;
	int *address = (int*) segbase;
        //printf("Add from RVM %d\n",*((int*)rvm->segment[0]->address));	
	for(i = 0; i < MAX_SEGMENT; i++){	
	if( *((int*)rvm->segment[i]->address) == *((int*)address) )
	break;
	}
	//printf("Index is %d\n",i);
	return  (i == (MAX_SEGMENT-1) )? -1 : i; 
	
}

void rvm_unmap(rvm_t rvm, void *segbase)
{
	int index;
	//printf("Address is %d\n",*((int*)segbase));
	if( (index = index_from_address(rvm,segbase)) == -1 ){
	fprintf(stderr,"Could not resolve index for the segment\n");
	exit(EXIT_FAILURE);
	}

	//printf("Index : %d\n",index);
	if(munmap(segbase,rvm->segment[index]->size)){
	fprintf(stderr,"Could not unmap the segment : %s\n",strerror(errno));
	exit(EXIT_FAILURE);
	}
	return;
}

int index_from_name(rvm_t rvm, const char* segment)
{
	int i;
	for(i = 0; i < MAX_SEGMENT; i++){
	if(!strcmp(rvm->segment[i]->name,segment))
	break;
	}
	//printf("Index %d\n",i);	
	return  (i == (MAX_SEGMENT-1) )? -1 : i; 
}
void rvm_destroy(rvm_t rvm, const char* segment)
{
	int index = index_from_name(rvm,segment);
	//printf("Found the index : %d\n",index);
	if(remove(rvm->segment[index]->path)){
	fprintf(stderr,"Couldn't remove %s : %s\n",segment,strerror(errno));
	exit(EXIT_FAILURE);
	}
}


static void block_signal()
{
        sigset_t set;

        /*XXX Allow SIGINT & SIGTERM*/
	/* Block the signal */
        sigfillset(&set);
	sigdelset(&set,SIGINT);
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

