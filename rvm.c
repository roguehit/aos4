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
static int trans_index = 0;

/*Structure to keep track of all transactions*/
GlobalTrans* globalTrans;
 
rvm_t rvm_init(const char *directory){

	rvm_t RVM = (struct __rvm_t *) malloc (sizeof (struct __rvm_t));
	RVM->segNo = 0;
	
	globalTrans = (GlobalTrans*) malloc (sizeof(GlobalTrans));
	
	globalTrans->globalCount = 0;		
	/*Storing the Backup Directory path*/
	sprintf(RVM->dir,"%s/%s",getenv("PWD"),directory);
	//printf("Dir:%s\n",RVM->dir);

	/*Creating Backup Directory*/
	if(mkdir(RVM->dir,S_IRUSR | S_IWUSR | S_IXUSR)){
		if(errno == EEXIST){
			fprintf(stderr,"Directory Already Exits\n");
		}
		else
			fprintf(stderr,"Creation of Directory failed : %s\n",strerror(errno));
		/*Handle Error from MKDIR*/
	}

	char filename[500];
	sprintf(filename,"%s/test.log",RVM->dir); 

	if( (RVM->log = fopen (filename,"a+")) == NULL ){
		fprintf(stderr,"Init Failed, Shutting Down\n");
		exit(EXIT_FAILURE);
	}
	
	fclose(RVM->log);
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

	//printf("Checking if %s exists\n",segname);

	if(index_from_name(rvm,segname) == -1) 
		return FALSE;

	return TRUE;

}
/*
Type 0 = Update
Type 1 = Commit
.....
.....
*/

void update_backing_store(rvm_t rvm,const char *segname,int type)
{
	/*Read the entire File to a string*/

	struct stat info;
	int fd;
	char update_type[10];
	char filename[500];
        sprintf(filename,"%s/test.log",rvm->dir);
	
        if( (rvm->log = fopen (filename,"a+")) == NULL ){
                fprintf(stderr,"Init Failed, Shutting Down\n");
                exit(EXIT_FAILURE);
        }

	if( (fd = fileno(rvm->log)) == -1){
		fprintf(stderr,"Could not find descriptor :%s\n",strerror(errno));
		exit(EXIT_FAILURE);
	}


	fstat(fd,&info);

	if(info.st_size == 0 ){
		fprintf(stderr,"Log is empty\n");
		return;
	}

	rewind(rvm->log);

	char* log_buffer = (char*) malloc(info.st_size + 1);
	char* log_buffer1 = (char*) malloc(info.st_size + 1);
	
	switch(type){
	case 0: 
	strcpy(update_type,"update");
	break;
	
	case 1:
	strcpy(update_type,"commit");
	break;
	
	default:
	assert(0);
	}
	
	
	while( fgets(log_buffer,info.st_size,rvm->log) != NULL){
		strcpy(log_buffer1,log_buffer);
		long int head = 0;
		char* string = NULL;
		string = strtok(log_buffer,"|");

		if(!strcmp(string,segname)){
			//	printf("Now backing up segment %s\n",segname);

			/*Extract TimeStamp*/
			string = strtok(NULL,"|");
			char transeg[200];
			strcpy(transeg,string);	

			string = strtok(NULL,"|");
			unsigned int timestamp = atoi(string);

			/*Extract Type of Operation*/
			string = strtok(NULL,"|");
			head+=strlen(string) + 1;
			char type[20];
			strcpy(type,string);	

			/*Extract Offset*/
			string = strtok(NULL,"|");
			head+=strlen(string) + 1;
			int offset = atoi(string);

			/*Extract Size*/
			string = strtok(NULL,"|");
			head+=strlen(string) + 1;
			int size = atoi(string);

			/*Extract Data*/	
			string = strtok(NULL,"|");
			head+=strlen(string) + 5;
			char* data = (char*) malloc(size + 1);
			strcpy(data,string);


			/*Write only on update record*/
			if(!strcmp(type,update_type)){
	printf("%s\n",log_buffer1);
	printf("Log file FD:%d,Back Type:%s,Segname:%s\n",fd,update_type,segname);	
				char filepath[200];
				sprintf(filepath,"%s/%s",rvm->dir,segname);

				int segfd = open(filepath,O_RDWR | O_EXCL,(mode_t)0600);

				if(segfd < 0){
					fprintf(stderr,"Could not open existing segment: %s\n",strerror(errno));
					exit(EXIT_FAILURE);
				}

				long int tell = ftell(rvm->log);	

				printf("Current %ld, Seek to %ld\n",tell,head);

				if( lseek(segfd,offset,SEEK_SET) != offset){
					fprintf(stderr,"Segment file seek failed : %s",strerror(errno));
					exit(EXIT_FAILURE);
				}
				write(segfd,data,size);
				
        			close(segfd);	
#if 0
				//rewind(rvm->log);
				int ret = -1;
				//rewind(rvm->log);
				if( (ret = lseek(fd,tell-head,SEEK_SET)) != (tell-head) ) {
					fprintf(stderr,"Seek Failed:%s, Ret:%d\n",strerror(errno),ret);
					exit(EXIT_FAILURE);
				}
				printf("Writing at %d, Current Pos %ld\n",ret,ftell(rvm->log));
				//write(fd,(void*)"commit",6);


				//rewind(rvm->log);
				//fseek(rvm->log,tell,SEEK_SET);
#endif
			}
		}
	}
        fclose(rvm->log);	
	return;
}

void *rvm_map(rvm_t rvm, const char *segname, int size_to_create){

	char dirfile[500];
	memSeg *seg;
	char filepath[500];
	sprintf(dirfile,"%s/test.dir",rvm->dir);
	int temp;
	int dirfp = open (dirfile,O_RDWR | O_CREAT , S_IRWXU | S_IRWXG);
#if 1
	if(dirfp == -1){
		fprintf(stderr,"Could not create dummy directory file to lock\n");
	}

	if(lockfile(dirfp) == -1){
		fprintf(stderr,"Lock Failed\n");
		exit(EXIT_FAILURE);
	}
#endif
	//update_backing_store(rvm,segname,0);

	seg = (memSeg*) malloc(sizeof(memSeg));
	//	printf("Here\n");
	strcpy(seg->name,segname);

	sprintf(filepath,"%s/%s",rvm->dir,segname);
	strcpy(seg->path,filepath);
	/*Open a segment*/
	seg->fd = open(filepath,O_RDWR | O_CREAT | O_EXCL,(mode_t)0600);
	/*BrandNew Segment File*/
	if( seg->fd != -1 ){
		//	printf("Here\n");
		seg->size = size_to_create;
		//write(seg->fd,(void*)"testing",1);	
		//printf("FD:%d\n",seg->fd);		
		//temp = lseek(seg->fd,seg->size-1,SEEK_SET);

		fill_region(seg->fd,size_to_create);
		temp = lseek(seg->fd,seg->size-1,SEEK_SET);
		if(temp == -1){
			fprintf(stderr,"Seek Failed : %s\n",strerror(errno));
			close(seg->fd);
			exit(EXIT_FAILURE);
		}
#if 0
		temp = write(seg->fd,"",1);
		if(temp == -1){
			fprintf(stderr,"Write Failed : %s\n",strerror(errno));
			close(seg->fd);
			exit(EXIT_FAILURE);
		}
#endif
		//printf("FD:%d\n",seg->fd);

		seg->address = mmap(0,size_to_create,
				PROT_READ | PROT_WRITE,
				MAP_PRIVATE,
				seg->fd,0);


		//	printf("Here\n");
		if(seg->address == MAP_FAILED ){
			fprintf(stderr,"mmap failed:%s on %d\n",strerror(errno),__LINE__);
			unlockfile(dirfp);
			close(dirfp);
			close(seg->fd);
			return (rvm_t)-1;
		}else{
			//printf("%d",(size_t)size_to_create);
			printf("New Segment %s at %08x, filepath %s FD %d\n",seg->name,(int)(int*)seg->address,filepath,seg->fd);
			//memset(seg->address,1,(size_t)size_to_create);
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
			printf("Opening Old Segment %s at %08x address\n",seg->name,(int)((int*)seg->address));
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

		//if(rvm->segment[i]->address == NULL)
		//continue; 

		if( (int*)rvm->segment[i]->address == address ){
			printf("Returning now %d : %s\n",i,rvm->segment[i]->name);
			return i;
		}
	}
	//printf("Index is %d\n",i);
	return -1; 

}

static int index_from_address2(trans_t tid, void* segbase)
{
	int ret;
	if( (ret = index_from_address(tid->rvm,segbase)) == -1)
	return -1;
	else 
	return ret;	
}

void rvm_unmap(rvm_t rvm, void *segbase)

{
	int index;
	//printf("Address %d is of %s\n",((int*)segbase),rvm->segment[index_from_address(rvm,segbase)]->name );
	if( (index = index_from_address(rvm,segbase)) == -1 ){
		fprintf(stderr,"Could not resolve index for the segment\n");
		exit(EXIT_FAILURE);
	}

	printf("Unmapping Segment %s at Index : %d\n",rvm->segment[index]->name,index);
	if(munmap(segbase,rvm->segment[index]->size)){
		fprintf(stderr,"Could not unmap the segment : %s\n",strerror(errno));
		exit(EXIT_FAILURE);
	}

	/*Clean up the Structure Fields*/
	rvm->segment[index]->address = NULL;
	rvm->segment[index]->fd = -1;

	return;
}

int index_from_name(rvm_t rvm, const char* segment)
{
	int i;
	//printf("before\n");	
	for(i = 0; i < rvm->segNo ;i++){

		//if(rvm->segment[i]->address == NULL)
		//continue;

		if(!strcmp(rvm->segment[i]->name,segment))
			return i;

	}
	//printf("Index %d\n",i);	
	return -1; 
}
void rvm_destroy(rvm_t rvm, const char* segment)
{
	int index = index_from_name(rvm,segment);
	//printf("Found the index : %d\n",index);
	if(remove(rvm->segment[index]->path)){
		if(errno != 2){
		fprintf(stderr,"Couldn't remove %s : %s\n",segment,strerror(errno));	
		exit(EXIT_FAILURE);
		}
	}
}
#if 1
void rvm_commit_trans(trans_t tid) 
{
	printf("Copying to backing store/commiting data\n");
	int i;
	
	char filename[500];
        sprintf(filename,"%s/test.log",tid->rvm->dir);

        if( (tid->rvm->log = fopen (filename,"a+")) == NULL ){
                fprintf(stderr,"Init Failed, Shutting Down\n");
                exit(EXIT_FAILURE);
        }

	struct timeval time1;
	unsigned long int epoch;
	gettimeofday(&time1,NULL);
	epoch = time1.tv_sec * 1000 + time1.tv_usec/1000;
	//printf("%ld\n",epoch);	
	/* Lock the log file and write tinto it */
	//flock(fileno(tid->rvm->log), LOCK_EX);
	for(i = 0; i < tid->last_index; i++){
	
	int segIndex = tid->transSeg[i]->mainIndex;
	
	if (fprintf(tid->rvm->log, "%s|transeg%d|%lu|commit|%d|%d|",tid->rvm->segment[segIndex]->name,tid->transSeg[i]->global_trans_index,epoch,tid->transSeg[i]->offset,tid->transSeg[i]->size) < 0){
		fprintf(stderr,"fprintf error");
		//flock(fileno(tid->rvm->log), LOCK_UN);
		abort();
	}


	fwrite((char *)tid->rvm->segment[segIndex]->address + tid->transSeg[i]->offset , 1, tid->transSeg[i]->size, tid->rvm->log);
	fprintf(tid->rvm->log, "|EOL|\n");
	//write to disk
	printf("Commit Index : %d\n",i);
	}	


	fdatasync(fileno(tid->rvm->log));
	//flock(fileno(tid->rvm->log), LOCK_UN);
		
        fclose(tid->rvm->log);	
	printf("Done Copying\n");

		
	for(i = 0; i < tid->numsegs ; i++)
	{
	
	close(tid->rvm->segment[i]->fd);

	printf("Update Index : %d\n",i);
	update_backing_store(tid->rvm,tid->rvm->segment[i]->name,1);
	}
	
	/*Change State of Transaction to Done*/
	tid->state = 1;	
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
	tid->state = 0; 


	globalTrans->tid[globalTrans->globalCount] = tid;
	globalTrans->globalCount++;		

	printf("Running through %d Segments\n",rvm->segNo);
	for(i = 0; i < rvm->segNo; i++)
	{
		/*For Segments which have been unmapped*/
		if(rvm->segment[i]->address == NULL){	
			printf("Address of segment at index %d is NULL\n",i);
			continue;
		}
		for(j = 0; j<numsegs; j++)
		{
			fprintf(stderr, "Rvm Address:%08x\n",(int)((int *)(rvm->segment[i]->address)));
			void *seg = segbases[j];

			//fprintf(stderr, "Rvm Segment %d Address:%08x SegAddress:%08x\n",i,(int)(int *)(rvm->segment[i]->address),(int)(int*)(seg));
			if( (int)((int*)rvm->segment[i]->address) ==  (int)((int*)(seg)))
			{
				if(rvm->segment[i]->locked == 1)
				{
					fprintf(stderr,"Memory Segment at %08x locked", (int)((int*)rvm->segment[i]->address));
					return (trans_t) -1;
				}		
				else
				{
					fprintf(stderr,"Lockig mem Segment at %08x\n", (int)((int*)rvm->segment[i]->address));
					rvm->segment[i]->locked = 1;
					//tid->last_index++;
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

	struct __transSeg *transSeg = (struct __transSeg*)malloc(sizeof(struct __transSeg));
	
	transSeg->offset = offset;
	transSeg->size = size;
	transSeg->mainIndex = segIndex;
	transSeg->global_trans_index = trans_index;
	trans_index++;
	
	tid->transSeg[tid->last_index++] = transSeg;
	
	char filename[500];
        sprintf(filename,"%s/test.log",tid->rvm->dir);

        if( (tid->rvm->log = fopen (filename,"a+")) == NULL ){
                fprintf(stderr,"Init Failed, Shutting Down\n");
                exit(EXIT_FAILURE);
        }

	/*get the current time*/
	struct timeval time1;
	unsigned long int epoch;
	gettimeofday(&time1,NULL);
	epoch = time1.tv_sec * 1000 + time1.tv_usec/1000;
	//printf("%ld\n",epoch);	
	/* Lock the log file and write tinto it */
	//flock(fileno(tid->rvm->log), LOCK_EX);
	
	if (fprintf(tid->rvm->log, "%s|transseg%d|%lu|update|%d|%d|",tid->rvm->segment[segIndex]->name,transSeg->global_trans_index,epoch,offset, size) < 0){
		printf("haga\n");
		perror("fprintf error");
		flock(fileno(tid->rvm->log), LOCK_UN);
		abort();
	}
	//fprintf(stderr,"here");
	fwrite((char *)segbase + offset , 1, size, tid->rvm->log);
	fprintf(tid->rvm->log, "|EOL|\n");
	//write to disk
	fdatasync(fileno(tid->rvm->log));
	//flock(fileno(tid->rvm->log), LOCK_UN);
        fclose(tid->rvm->log);	
	return;
}

int rvm_query_uncomm(rvm_t rvm, trans_t *tids)
{
	int i;
	int ret = 0;

	for(i = 0 ; i < globalTrans->globalCount ; i++ )
	if(globalTrans->tid[i]->state == 0)
	ret++;

	if(ret!=0){
	tids = (struct __trans_t **) malloc (sizeof(struct __trans_t*) * ret);
	for(i = 0 ; i < ret ; i++)	
	tids[i] = (struct __trans_t*) malloc(sizeof(struct __trans_t));
	}

return ret;
}

