#ifndef RVM_HEADER_H
#define RVM_HEADER_H

#include <stdio.h>

#define MAX_SEGMENT 10

typedef struct __memSeg{
 char name[200];
 void *address;
 int fd;
 int size;
 char path[200];
 int locked;
} memSeg;

struct __rvm_t{
 char dir[500];
 int segNo;
 FILE *log;
 memSeg* segment[MAX_SEGMENT];
};
typedef struct __rvm_t* rvm_t; 

struct __transSeg{
//memSeg* segment;
int mainIndex;
int offset;
int size;
};

struct __trans_t{
	rvm_t rvm;
	int numsegs;
	//int segArr[MAX_SEGMENT];
	struct __transSeg* transSeg[MAX_SEGMENT];
	//memSeg* segment[MAX_SEGMENT];
	int last_index;
};

/* TO be done - Tree Node Structure and Tree */

typedef struct __trans_t* trans_t;
/* RVM functions */
extern rvm_t rvm_init(const char *directory);
extern void *rvm_map(rvm_t rvm, const char *segname, int size_to_create);
extern void rvm_unmap(rvm_t rvm, void *segbase);
extern void rvm_destroy(rvm_t rvm, const char *segname);

/*Transaction functions*/
extern trans_t rvm_begin_trans(rvm_t rvm, int numsegs, void **segbases);
extern void rvm_about_to_modify(trans_t tid, void *segbase, int offset, int size);
extern void rvm_commit_trans(trans_t tid);
extern void rvm_abort_trans(trans_t tid);
extern int rvm_query_uncomm(rvm_t rvm, const char* segname, trans_t **tids);
#endif
