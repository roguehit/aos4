#ifndef RVM_HEADER_H
#define RVM_HEADER_H

#include <stdio.h>

#define MAX_SEGMENT 100

typedef struct __memSeg{
 char name[200];
 void *address;
 int fd;
 int size;
 char path[200];
} memSeg;

struct __rvm_t{
 char dir[500];
 int segNo;
 FILE *log;
 memSeg* segment[MAX_SEGMENT];
};

typedef struct __rvm_t* rvm_t; 
extern rvm_t rvm_init(const char *directory);
extern void *rvm_map(rvm_t rvm, const char *segname, int size_to_create);
extern void rvm_unmap(rvm_t rvm, void *segbase);
extern void rvm_destroy(rvm_t rvm, const char *segname);
#endif