#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

#define STACK_HEURISTIC 32
#define MAX_STACK_SIZE (1<<23)
/* 2^23 bits === 256 KB. */

/* Lock for file system calls. */
struct lock file_lock;

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);
bool install_page (void *, void *, bool);

#endif /* userprog/process.h */
