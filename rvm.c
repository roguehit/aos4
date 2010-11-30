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
static int filesize(int fd);
static int index_from_address(rvm_t rvm, void* segbase);
static int index_from_name (rvm_t rvm, const char* name);
static void handle_abort(rvm_t rvm);
static node* build_tree_abort(rvm_t rvm, node *root);
static void write_modify(rvm_t rvm);
static int trans_index = 0;
node *result;

/*
 * 0 - No Debug 
 * 1 - Debug On (Default)
 */
int debugEnableFlag = 1;


/*Structure to keep track of all transactions*/
GlobalTrans* globalTrans;

rvm_t rvm_init(const char *directory){

	rvm_t RVM = (struct __rvm_t *) malloc (sizeof (struct __rvm_t));
	RVM->segNo = 0;

	globalTrans = (GlobalTrans*) malloc (sizeof(GlobalTrans));

	globalTrans->globalCount = 0;		
	/*Storing the Backup Directory path*/
	sprintf(RVM->dir,"%s/%s",getenv("PWD"),directory);

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
	int fd;
	if( (fd = fileno(RVM->log)) == -1){
		fprintf(stderr,"Could not find descriptor :%s\n",strerror(errno));
		exit(EXIT_FAILURE);
	}
	int fsize = filesize(fd);	
	fclose(RVM->log);

	if(debugEnableFlag)
		fprintf(stderr,"Log file size:%d\n",fsize); 
	if(fsize > 0)
		rvm_truncate_log(RVM);

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

int filesize(int fd)
{
	struct stat info;

	if( fstat(fd,&info) == -1){
		fprintf(stderr,"File Suddenly Disappeared\n");
		//exit(EXIT_FAILURE);
		return -1;
	}
	/*
	 *Making Sure the file is not special
	 *and does not have more than 1 hardlink
	 */
	assert(info.st_nlink == 1);

	return info.st_size;
}

int check_exist(rvm_t rvm, char* segname)
{
	if(rvm->segNo == 0)
		return FALSE;

	if(index_from_name(rvm,segname) == -1) 
		return FALSE;

	return TRUE;

}

void *rvm_map(rvm_t rvm, const char *segname, int size_to_create){


	/*Crash Recovery*/
	if(rvm->recovery_tree != NULL)
		if(debugEnableFlag)
			fprintf(stderr, "Recovering Segment of region %s\n", rvm->recovery_tree->data.name);
	/*End of Crash Recovery*/	
	char dirfile[500];
	memSeg *seg;
	char filepath[500];
	sprintf(dirfile,"%s/test.dir",rvm->dir);
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


	seg = (memSeg*) malloc(sizeof(memSeg));
	strcpy(seg->name,segname);
	sprintf(filepath,"%s/%s",rvm->dir,segname);
	strcpy(seg->path,filepath);

	/*Open a segment*/
	seg->fd = open(filepath,O_RDWR | O_CREAT | O_EXCL,(mode_t)0600);
	/*BrandNew Segment File*/
	if( seg->fd != -1 ){

		seg->size = size_to_create;

		int temp;
		fill_region(seg->fd,size_to_create);
		temp = lseek(seg->fd,seg->size-1,SEEK_SET);
		if(temp == -1){
			fprintf(stderr,"Seek Failed : %s\n",strerror(errno));
			close(seg->fd);
			exit(EXIT_FAILURE);
		}


		seg->address = mmap(0,size_to_create,
				PROT_READ | PROT_WRITE,
				MAP_PRIVATE,
				seg->fd,0);



		if(seg->address == MAP_FAILED ){
			fprintf(stderr,"mmap failed:%s on %d\n",strerror(errno),__LINE__);
			unlockfile(dirfp);
			close(dirfp);
			close(seg->fd);
			return (rvm_t)-1;
		}else{

			if(debugEnableFlag)
				printf("New Segment %s at %08x, filepath %s FD %d\n",seg->name,(int)(int*)seg->address,filepath,seg->fd);
		}

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
			if(debugEnableFlag)
				printf("Opening Old Segment %s at %08x address\n",seg->name,(int)((int*)seg->address));
		}else if(size < size_to_create){
			seg->size = size_to_create;	

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
			if(ftruncate(seg->fd,seg->size) != 0){
				fprintf(stderr,"Ftruncate Failed:%s,Line:%d",strerror(errno),__LINE__);
				exit(EXIT_FAILURE);	
			}
			int temp;	
			temp = lseek(seg->fd,seg->size-1,SEEK_SET);
			write(seg->fd,"",1);
		}

	}
	unlockfile(dirfp);
	close(dirfp);

	rvm->segment[rvm->segNo] = seg;

	int segno = rvm->segNo;
	rvm->segNo++;

	write_modify(rvm);
	return rvm->segment[segno]->address;
}

static int index_from_address(rvm_t rvm, void* segbase)
{
	int i;
	int *address = (int*) segbase;

	for(i = 0; i < MAX_SEGMENT; i++){	

		if( (int*)rvm->segment[i]->address == address ){
			if(debugEnableFlag)
				printf("Returning now %d : %s\n",i,rvm->segment[i]->name);
			return i;
		}
	}

	return -1; 

}


void rvm_unmap(rvm_t rvm, void *segbase)

{
	int index;
	if( (index = index_from_address(rvm,segbase)) == -1 ){
		fprintf(stderr,"Could not resolve index for the segment\n");
		exit(EXIT_FAILURE);
	}

	if(debugEnableFlag)
		printf("Unmapping Segment %s at Index : %d\n",rvm->segment[index]->name,index);
	if(munmap(segbase,rvm->segment[index]->size)){
		fprintf(stderr,"Could not unmap the segment : %s\n",strerror(errno));
		exit(EXIT_FAILURE);
	}

	/*Clean up the Structure Fields*/
	rvm->segment[index]->address = NULL;
	rvm->segment[index]->locked = 0;
	rvm->segment[index]->fd = -1;
	strcpy(rvm->segment[index]->name,""); 

	return;
}

int index_from_name(rvm_t rvm, const char* segment)
{
	int i;

	for(i = 0; i < rvm->segNo ;i++){

		if(!strcmp(rvm->segment[i]->name,segment))
			return i;

	}

	return -1; 
}
void rvm_destroy(rvm_t rvm, const char* segment)
{
	int index = index_from_name(rvm,segment);

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

	if(debugEnableFlag)
		printf("Commiting Data / Writing to Log File\n");
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

	flock(fileno(tid->rvm->log), LOCK_EX);
	for(i = 0; i < tid->last_index; i++){

		int segIndex = tid->transSeg[i]->mainIndex;

		if (fprintf(tid->rvm->log, "%s|transseg%d|%lu|commit|%d|%d|",tid->rvm->segment[segIndex]->name,tid->transSeg[i]->global_trans_index,epoch,tid->transSeg[i]->offset,tid->transSeg[i]->size) < 0){
			fprintf(stderr,"fprintf error");
			flock(fileno(tid->rvm->log), LOCK_UN);
			abort();
		}


		fwrite((char *)tid->rvm->segment[segIndex]->address + tid->transSeg[i]->offset , 1, tid->transSeg[i]->size, tid->rvm->log);
		fprintf(tid->rvm->log, "|EOL|\n");
		//write to disk
		if(debugEnableFlag)
			printf("Commit Index : %d\n",i);

		tid->rvm->segment[segIndex]->locked = 0;
	}	


	fdatasync(fileno(tid->rvm->log));
	flock(fileno(tid->rvm->log), LOCK_UN);

	if(debugEnableFlag)	
		printf("Done Commiting\n");


	for(i = 0; i < tid->numsegs ; i++)
	{

		close(tid->rvm->segment[i]->fd);

		if(debugEnableFlag)
			printf("Update Index : %d\n",i);

	}

	/*Change State of Transaction to DONE*/
	tid->state = 1;

	int fsize = filesize(fileno(tid->rvm->log));
	int records = 0;


	if(fsize > 0){

		char *temp = (char*) malloc (fsize+10);
		rewind(tid->rvm->log);

		while( fgets(temp,fsize+10,tid->rvm->log) != NULL)
			records++;


		fclose(tid->rvm->log);

		if(records > 10)	
			rvm_truncate_log(tid->rvm);

	}

	return;	
}
#endif
void rvm_abort_trans(trans_t tid) 
{
	if(debugEnableFlag)
		fprintf(stderr, "Aborting Transaction...Restoring Previous State\n");
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

	/* Lock the log file and write tinto it */
	flock(fileno(tid->rvm->log), LOCK_EX);
	for(i = 0; i < tid->last_index; i++){

		int segIndex = tid->transSeg[i]->mainIndex;

		if (fprintf(tid->rvm->log, "%s|transseg%d|%lu|abort|%d|%d|",tid->rvm->segment[segIndex]->name,tid->transSeg[i]->global_trans_index,epoch,tid->transSeg[i]->offset,tid->transSeg[i]->size) < 0){
			fprintf(stderr,"fprintf error");
			flock(fileno(tid->rvm->log), LOCK_UN);
			abort();
		}

		fprintf(tid->rvm->log, "|EOL|\n");
		//write previous state to memory
		tid->rvm->segment[segIndex]->locked = 0;
	}	

	fdatasync(fileno(tid->rvm->log));
	flock(fileno(tid->rvm->log), LOCK_UN);

	fclose(tid->rvm->log);	
	if(debugEnableFlag)
		printf("Abort Complete\n");

	/*Write previous state to memory*/

	handle_abort(tid->rvm);

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
	if(debugEnableFlag)
		printf("Running through %d Segments\n",rvm->segNo);
	for(i = 0; i < rvm->segNo; i++)
	{
		/*For Segments which have been unmapped*/
		if(rvm->segment[i]->address == NULL){	
			if(debugEnableFlag)
				printf("Address of segment at index %d is NULL\n",i);
			continue;
		}
		for(j = 0; j<numsegs; j++)
		{
			if(debugEnableFlag)
				fprintf(stderr, "Rvm Address:%08x\n",(int)((int *)(rvm->segment[i]->address)));
			void *seg = segbases[j];

			if( (int)((int*)rvm->segment[i]->address) ==  (int)((int*)(seg)))
			{
				if(rvm->segment[i]->locked == 1)
				{
					fprintf(stderr,"Memory Segment at %08x locked", (int)((int*)rvm->segment[i]->address));
					return (trans_t) -1;
				}		
				else
				{
					if(debugEnableFlag)
						fprintf(stderr,"Lockig mem Segment at %08x\n", (int)((int*)rvm->segment[i]->address));
					rvm->segment[i]->locked = 1;
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

	/* Lock the log file and write tinto it */
	flock(fileno(tid->rvm->log), LOCK_EX);

	if (fprintf(tid->rvm->log, "%s|transseg%d|%lu|update|%d|%d|",tid->rvm->segment[segIndex]->name,transSeg->global_trans_index,epoch,offset, size) < 0){

		perror("fprintf error");
		flock(fileno(tid->rvm->log), LOCK_UN);
		abort();
	}

	fwrite((char *)segbase + offset , 1, size, tid->rvm->log);
	fprintf(tid->rvm->log, "|EOL|\n");

	//write to disk
	fdatasync(fileno(tid->rvm->log));
	flock(fileno(tid->rvm->log), LOCK_UN);
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

/*Log Functions*/
void handle_abort(rvm_t rvm)
{
	/* build recovery tree*/
	rvm->recovery_tree = NULL;
	rvm->recovery_tree = build_tree_abort(rvm, rvm->recovery_tree);
}

void rvm_truncate_log(rvm_t rvm)
{
	/* build recovery tree*/
	rvm->recovery_tree = NULL;
	rvm->recovery_tree = build_tree(rvm, rvm->recovery_tree);
	if(rvm->recovery_tree == NULL)
		return;
	else
	{
		printout(rvm->recovery_tree);
		/* clear the log file and update contents*/
		if(rvm_update_log(rvm)< 0)
		{
			fprintf(stderr, "Error updating truncating log file\n");
		}
	}

}
int rvm_update_log(rvm_t rvm)
{
	char filename[500];
	sprintf(filename,"%s/test.log",rvm->dir);
	if( (rvm->log = fopen (filename,"w")) == NULL ){
		fprintf(stderr,"Init Failed, Shutting Down\n");
		exit(EXIT_FAILURE);
	}
	FILE *log = rvm->log;
	node *tree = rvm->recovery_tree;
	sync_tree_log(tree,log);
	fclose(rvm->log);
	return 0;	
}
/*Tree functions */
/*List Functions*/
node * insert_sibling(node *head, struct node_info data)
{
	node * curr, *temp;
	curr = (node *)malloc(sizeof(node));
	curr->data = data;
	if(head == NULL)
		head = curr;
	else
	{
		temp = head;
		while(temp->next != NULL)
			temp=temp->next;
		temp->next = curr;
	}
	return head;

}
/*End of List Functions*/


void search_by_name(node *tree,char* nodename)
{

	if(tree->first_child) search_by_name(tree->first_child,nodename);
	if(!strcmp(tree->data.name,nodename))
	{
		result = tree;
		//fprintf(stderr, "Result: %s\n", tree->data.name);
	}
	if(tree->next) search_by_name(tree->next,nodename);
}
/* Tree Functions */
node* insert(node * tree, node * item, char* parentname) {

	if(!(tree)) {
		tree = item;
		return tree;
	}

	else
	{

		result = (node*)malloc(sizeof(node));
		search_by_name(tree,parentname);
		node *parent = result;
		assert(parent!=NULL);

		if(parent->first_child ==NULL)
		{		
			parent->first_child = item;
		}
		else
		{
			parent->first_child = insert_sibling(parent->first_child,item->data);
		}
	}
	return tree;
}


void printout(node * tree) {
	if(tree->first_child) printout(tree->first_child);
	if(debugEnableFlag)
		printf("Name:%s Segment:%s State:%d\n",tree->data.name,tree->data.segname,tree->data.type);
	if(tree->next) printout(tree->next);
}

node* build_tree(rvm_t rvm, node *root)
{

	struct stat info;
	struct node_info temp_info;
	node * new_node;
	int fd;
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
		if(debugEnableFlag)
			fprintf(stderr,"Log is empty\n");
		return NULL;
	}

	rewind(rvm->log);

	/*Create root node*/
	new_node = (node*)malloc(sizeof(node));
	strcpy(new_node->data.name,rvm->dir);
	root = insert(root,new_node,NULL);

	char* log_buffer = (char*) malloc(info.st_size + 1);
	char* log_buffer1 = (char*) malloc(info.st_size + 1);

	/* Parse log file */
	new_node = (node*)malloc(sizeof(node));
	while( fgets(log_buffer,info.st_size,rvm->log) != NULL){
		strcpy(log_buffer1,log_buffer);
		if(strlen(log_buffer) > 1)
		{
		long int head = 0;
		char* string = NULL;

		/*Extract Segname */
		string = strtok(log_buffer,"|");
		char segname[500];
		strcpy(segname,string);
		strcpy(temp_info.segname,segname);

		temp_info.type = 0;
		result = NULL;
		search_by_name(root,segname);
		if(result==NULL)
		{
			new_node = (node*)malloc(sizeof(node));
			strcpy(temp_info.name,segname);
			new_node->data = temp_info;
			root = insert(root,new_node,root->data.name);
		}
		new_node = (node*)malloc(sizeof(node));	
		/*Extract Transaction Name*/
		string = strtok(NULL,"|");
		char nodename[50];
		strcpy(nodename,string);
		strcpy(temp_info.name,nodename);

		/*Extract TimeStamp*/
		string = strtok(NULL,"|");
		unsigned int timestamp = atoi(string);
		temp_info.timestamp = timestamp;

		/*Extract Type of Operation*/
		string = strtok(NULL,"|");
		head+=strlen(string) + 1;
		char type[20];
		strcpy(type,string);
		if(!strcmp(type,"update"))
			temp_info.type = 1;
		else if(!strcmp(type,"commit"))
			temp_info.type = 2;
		else 
			temp_info.type = 3;

		/*Extract Offset*/
		string = strtok(NULL,"|");
		head+=strlen(string) + 1;
		int offset = atoi(string);
		temp_info.offset = offset;

		/*Extract Size*/
		string = strtok(NULL,"|");
		head+=strlen(string) + 1;
		int size = atoi(string);
		temp_info.size = size;

		/*Extract Data*/	
		string = strtok(NULL,"|");
		head+=strlen(string) + 5;
		char* data = (char*) malloc(size + 1);
		temp_info.data = (char*)malloc(size + 1);
		strcpy(data,string);
		strcpy(temp_info.data,data);

		new_node->data = temp_info;	

		if(temp_info.type == 2)
		{
			//fprintf(stderr,"Update Node Name:%s\n", temp_info.name);


			search_by_name(root,temp_info.name);
			node *update_node = result;
			assert(update_node != NULL);
			if(update_node != NULL)
			{
				char filepath[200];
				sprintf(filepath,"%s/%s",rvm->dir,temp_info.segname);

				int segfd = open(filepath,O_RDWR | O_EXCL,(mode_t)0600);

				if(segfd < 0){
					fprintf(stderr,"Could not open existing segment: %s\n",strerror(errno));
					exit(EXIT_FAILURE);
				}

				long int tell = ftell(rvm->log);	
				if(debugEnableFlag)
					printf("Current %ld, Seek to %ld\n",tell,head);

				if( lseek(segfd,temp_info.offset,SEEK_SET) != offset){
					fprintf(stderr,"Segment file seek failed : %s",strerror(errno));
					exit(EXIT_FAILURE);
				}
				write(segfd,temp_info.data,temp_info.size);

				close(segfd);
				/*update the status of the update_node */
				update_node->data.type = 2;
				result->data.type = 2;

			}
			else
			{

				fprintf(stderr,"Error Severe: Commit without an update");
			}
			continue;
		}
		/*Handle Aborts*/
		if(temp_info.type ==3)
		{	
			result = (node*)malloc(sizeof(node));
			search_by_name(root,temp_info.name);
			if(result)
			{
				result->data.type = 3;
			}
			continue;
		}
		root = insert(root,new_node,segname);

	}
	}
	if(debugEnableFlag)
		fprintf(stderr, "Tree Created \n");
	fclose(rvm->log);		
	return root;
}

void sync_tree_log(node *tree, FILE *log)
{
	/*First Child*/
	if(tree->first_child) 
		sync_tree_log(tree->first_child, log);

	/*Node */
	if( (tree->data.type == 1))
	{	
		if(debugEnableFlag)
			fprintf(stderr,"Inside Node: %s\n", tree->data.name);
		char type[20];
		if(tree->data.type == 1)
			strcpy(type,"tomodify");

		if (fprintf(log, "%s|%s|%lu|%s|%d|%d|",tree->data.segname,tree->data.name,tree->data.timestamp,type,tree->data.offset, tree->data.size) < 0){

			perror("fprintf error");
			flock(fileno(log), LOCK_UN);
			abort();
		}

		fwrite((char *)tree->data.data , 1, tree->data.size, log);
		fprintf(log, "|EOL|\n");
		//write to disk
		fdatasync(fileno(log));
	}
	/*Sibling*/
	if(tree->next) 
		sync_tree_log(tree->next, log);
}
node* build_tree_abort(rvm_t rvm, node *root)
{

	struct stat info;
	struct node_info temp_info;
	node * new_node;
	int fd;
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
		if(debugEnableFlag)		
			fprintf(stderr,"Log is empty\n");
		return NULL;
	}

	rewind(rvm->log);

	/*Create root node*/
	new_node = (node*)malloc(sizeof(node));
	strcpy(new_node->data.name,rvm->dir);
	root = insert(root,new_node,NULL);

	char* log_buffer = (char*) malloc(info.st_size + 1);
	char* log_buffer1 = (char*) malloc(info.st_size + 1);

	/* Parse log file */
	new_node = (node*)malloc(sizeof(node));
	while( fgets(log_buffer,info.st_size,rvm->log) != NULL){
		strcpy(log_buffer1,log_buffer);
		if(strlen(log_buffer1) > 1)
		{
		long int head = 0;
		char* string = NULL;

		/*Extract Segname */
		string = strtok(log_buffer,"|");
		char segname[500];
		strcpy(segname,string);
		strcpy(temp_info.segname,segname);

		result = NULL;
		search_by_name(root,segname);
		if(result==NULL)
		{
			new_node = (node*)malloc(sizeof(node));
			strcpy(temp_info.name,segname);
			new_node->data = temp_info;
			root = insert(root,new_node,root->data.name);
		}
		new_node = (node*)malloc(sizeof(node));	
		/*Extract Transaction Name*/
		string = strtok(NULL,"|");
		char nodename[50];
		strcpy(nodename,string);
		strcpy(temp_info.name,nodename);

		/*Extract TimeStamp*/
		string = strtok(NULL,"|");
		unsigned int timestamp = atoi(string);
		temp_info.timestamp = timestamp;

		/*Extract Type of Operation*/
		string = strtok(NULL,"|");
		head+=strlen(string) + 1;
		char type[20];
		strcpy(type,string);
		if(!strcmp(type,"update"))
			temp_info.type = 1;
		else if(!strcmp(type,"commit"))
			temp_info.type = 2;
		else 
			temp_info.type = 3;

		/*Extract Offset*/
		string = strtok(NULL,"|");
		head+=strlen(string) + 1;
		int offset = atoi(string);
		temp_info.offset = offset;

		/*Extract Size*/
		string = strtok(NULL,"|");
		head+=strlen(string) + 1;
		int size = atoi(string);
		temp_info.size = size;

		/*Extract Data*/	
		string = strtok(NULL,"|");
		head+=strlen(string) + 5;
		char* data = (char*) malloc(size + 1);
		temp_info.data = (char*)malloc(size + 1);
		strcpy(data,string);
		strcpy(temp_info.data,data);
		new_node->data = temp_info;	
		if(temp_info.type ==2)
		{
			continue;
		}

		/*Handle Aborts*/
		if(temp_info.type ==3)
		{
			//fprintf(stderr,"Inside Handler");
			result = (node*)malloc(sizeof(node));
			search_by_name(root,temp_info.name);		

			if(result != NULL)
			{
				//fprintf(stderr, "Inside Abort handler, Node NameTemp: %s\n",temp_info.name );
				//fprintf(stderr, "Inside Abort handler, Node Name: %s\n",result->data.name );
				int segindex = index_from_name(rvm, temp_info.segname);
				memSeg* seg = (memSeg*)malloc(sizeof(memSeg));
				seg = rvm->segment[segindex];
				if(debugEnableFlag)
					fprintf(stderr,result->data.data);
				sprintf((char*)seg->address+result->data.offset,result->data.data);
			}
			else
				fprintf(stderr,"Error Severe: Abort without an update");
			continue;
		}
		root = insert(root,new_node,segname);
	}
	}
	//fprintf(stderr, "Tree Created \n");
	fclose(rvm->log);		
	return root;
}
void write_modify(rvm_t rvm)
{
	struct stat info;
	int fd;
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
		if(debugEnableFlag)
			fprintf(stderr,"Log is empty\n");
		return;
	}

	rewind(rvm->log);	

	char* log_buffer = (char*) malloc(info.st_size + 1);
	char* log_buffer1 = (char*) malloc(info.st_size + 1);
	while( fgets(log_buffer,info.st_size,rvm->log) != NULL){
		strcpy(log_buffer1,log_buffer);
		if(strlen(log_buffer) > 1) 
		{	
		long int head = 0;
		char* string = NULL;

		/*Extract Segname */
		string = strtok(log_buffer,"|");
		char segname[500];
		strcpy(segname,string);

		/*Extract Transaction Name*/
		string = strtok(NULL,"|");
		char nodename[50];
		strcpy(nodename,string);

		/*Extract TimeStamp*/
		string = strtok(NULL,"|");

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

		if(!strcmp(type,"tomodify"))
		{		
			int segindex = index_from_name(rvm, segname);
			memSeg* seg = (memSeg*)malloc(sizeof(memSeg));
			seg = rvm->segment[segindex];
			sprintf((char*)seg->address+offset,data);		
		}
	}
	}
	fclose(rvm->log);		
}

void rvm_verbose(int enable_flag)
{
	/*Flag is Binary Valued*/
	assert (enable_flag >=0 && enable_flag <=1);
	if(enable_flag)
		debugEnableFlag = 1;
	else
		debugEnableFlag = 0;
}

