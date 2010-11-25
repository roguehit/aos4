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
#include <sys/time.h>
#include "bool.h"
#include "rvm.h"

#define _GNU_SOURCE

static void block_signal();
static void unblock_signal();
static void fill_region(int fd , int size);
static int index_from_address(rvm_t rvm, void* segbase);
static int index_from_name (rvm_t rvm, const char* name);

rvm_t rvm_init(const char *directory){

	rvm_t RVM = (struct __rvm_t *) malloc (sizeof (struct __rvm_t));
	char filename[500];
	RVM->segNo = 0;
	/*Storing the Backup Directory path*/
	sprintf(RVM->dir,"%s/%s",getenv("PWD"),directory);
	//printf("Dir:%s\n",RVM->dir);

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
	//printf("Creating file %s\n",filename);

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

static void fill_region(int fd,int size)
{
	int i;
	
	for(i = 0; i < size;i++)
	if( write(fd,(void*)"t",1) == -1){
	fprintf(stderr,"Error : %s",strerror(errno));
	exit(EXIT_FAILURE);		
	}
return;
}
int check_exist(rvm_t rvm, char* segname)
{
	if(rvm->segNo == 0)
	return FALSE;
	else if( index_from_name(rvm,segname) == -1) 
	return FALSE;

	return TRUE;

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
		//write(seg->fd,(void*)"testing",1);	
	
		seg->address = mmap(NULL,size_to_create,
				PROT_READ | PROT_WRITE,
				MAP_PRIVATE,
				seg->fd,0);

		

	//	printf("Here\n");
		if(seg->address == (void*)-1){
			fprintf(stderr,"mmap failed:%s on %d\n",strerror(errno),__LINE__);
			unlockfile(dirfp);
			close(dirfp);
			return (rvm_t)-1;
		}else{
		//printf("%d",(size_t)size_to_create);
		//memset(seg->address,1,(size_t)size_to_create);
		fill_region(seg->fd,size_to_create);
		seg->fd = lseek(seg->fd,SEEK_SET,0);
		}
		//seg->fd = lseek(seg->fd,SEEK_SET,0);
	}else { 
		/*Backup Segment is already present*/
		if(check_exist(rvm,seg->name) == TRUE ){
		fprintf(stderr,"Segment has been mapped already\n");
		return (rvm_t)-1;
		}

		seg->fd = open(filepath,O_RDWR);
		int size = 0;
		if( (size = filesize(seg->fd)) == -1 ){
			fprintf(stderr,"File Disappeared ? \n");
			unlockfile(dirfp);
			close(dirfp);
			exit(EXIT_FAILURE);
		}
		//printf("Size of original segment:%d, Requested:%d\n",size,size_to_create);
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
	//printf("before\n");	
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
#if 0
void rvm_commit_trans(void* segment, int fd ,int size) 
{
	//printf("Copying to backing store\n");
	if(write(fd,segment,size) == -1){
	fprintf(stderr,"Commit Failed:%s\n",strerror(errno));
	exit(EXIT_FAILURE);
	}
return;	
}
#endif

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
trans_t rvm_begin_trans(rvm_t rvm, int numsegs, void **segbases)
{
	int i,j; 


	trans_t tid = (struct __trans_t *) malloc (sizeof (struct __trans_t));		
	tid->rvm = rvm;
	tid->numsegs = numsegs;
	tid->last_index = 0;
	for(i = 0; i < rvm->segNo; i++)
	{	
		for(j = 0; j<numsegs; j++)
		{
			//fprintf(stderr, "Rvm Address:%d",*((int *)(rvm->segment[i]->address)));
			void *seg = segbases[j];
			fprintf(stderr, "Rvm Address:%d SegAddress: %d\n",*((int *)(rvm->segment[i]->address)), *((int*)(seg)));
			if( *((int*)rvm->segment[i]->address) ==  *((int*)(seg)))
			{
				if(rvm->segment[i]->locked == 1)
				{
					fprintf(stderr,"Memory Segment at %d locked", *((int*)rvm->segment[i]->address));
					return (trans_t) -1;
				}		
				else
				{
					rvm->segment[i]->locked = 1;
					tid->segment[tid->last_index] = rvm->segment[i];
					tid->last_index++;
				}
			}
			
		}
	} 
	return tid;
}
void rvm_about_to_modify(trans_t tid, void *segbase, int offset, int size)
{
	/* Get the current segments index in the rvm*/
	int segIndex = index_from_address(tid->rvm, segbase);
	
	/*get the current time*/
	struct timeval time1;
	long int epoch;
	gettimeofday(&time1,NULL);
	epoch = time1.tv_sec * 1000 + time1.tv_usec/1000;
	printf("%ld\n",epoch);	
	/* Lock the log file and write tinto it */
	flock(fileno(tid->rvm->log), LOCK_EX);
  	if (fprintf(tid->rvm->log, "%ld|update|%s|%d|%d|", epoch,tid->rvm->segment[segIndex]->name,offset, size) < 0){
	    perror("fprintf error");
	    abort();
  	}
	//fprintf(stderr,"here");
  	fwrite((char *)segbase + offset , 1, size, tid->rvm->log);
  	fprintf(tid->rvm->log, "|EOL|\n");
  //write to disk
  	fdatasync(fileno(tid->rvm->log));
  	flock(fileno(tid->rvm->log), LOCK_UN);

  return;
}
