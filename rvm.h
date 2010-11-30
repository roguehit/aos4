#ifndef RVM_HEADER_H
#define RVM_HEADER_H

#include <stdio.h>

#define MAX_SEGMENT 10

struct node_info
{
	unsigned long int timestamp;
	char name[200];
	int type;//0-update, 1-commit
	int offset;
	char segname[200];
	int size;
	char *data;
};
struct tree_el {
   struct node_info data;
   struct tree_el * next, * first_child;
};
typedef struct tree_el node;
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
 node *recovery_tree;
};
typedef struct __rvm_t* rvm_t; 

struct __transSeg{
//memSeg* segment;
int mainIndex;
int offset;
int size;
int global_trans_index;
};

struct __trans_t{
	rvm_t rvm;
	int numsegs;
	//int segArr[MAX_SEGMENT];
	struct __transSeg* transSeg[MAX_SEGMENT];
	//memSeg* segment[MAX_SEGMENT];
	int last_index;
	
	/*State of Transaction
	* pending = 0
	* done = 1
	*/
	int state;
};

/* TO be done - Tree Node Structure and Tree */

typedef struct __trans_t* trans_t;

typedef struct __GlobalTrans{
	int globalCount;
	trans_t tid[MAX_SEGMENT];
}GlobalTrans;



/* RVM functions */
extern rvm_t rvm_init(const char *directory);
extern void *rvm_map(rvm_t rvm, const char *segname, int size_to_create);
extern void rvm_unmap(rvm_t rvm, void *segbase);
extern void rvm_destroy(rvm_t rvm, const char *segname);
extern void rvm_truncate_log(rvm_t rvm);
extern int rvm_update_log(rvm_t rvm);
/*Transaction functions*/
extern trans_t rvm_begin_trans(rvm_t rvm, int numsegs, void **segbases);
extern void rvm_about_to_modify(trans_t tid, void *segbase, int offset, int size);
extern void rvm_commit_trans(trans_t tid);
extern void rvm_abort_trans(trans_t tid);
extern int rvm_query_uncomm(rvm_t rvm, trans_t *tids);
extern void rvm_verbose(int enable_flag);

/*Tree functions*/
extern node * insert_sibling(node *head, struct node_info data);
extern void search_by_name(node *tree,char* nodename);
extern node* insert(node * tree, node * item, char* parentname);
extern void printout(node * tree);
extern node* build_tree(rvm_t rvm,node *root);
extern void sync_tree_log(node *tree, FILE *log);
#endif
