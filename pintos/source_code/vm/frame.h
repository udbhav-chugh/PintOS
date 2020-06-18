#ifndef VM_FRAME
#define VM_FRAME

#include <list.h>
#include "vm/page.h"
#include "threads/thread.h"
#include "threads/palloc.h"


//Defining the structure for a frame table entry
struct frame_table_entry
{
  void *frame;
  struct spt_entry *spte;
  struct thread *t;
  struct list_elem elem;  //Pointer to elements in frame table list
};


//Function declarations
static void frame_table_add (void *, struct spt_entry *);
void free_frame (void *);
void frame_table_init (void);
void *retrieve_frame_of_page (enum palloc_flags, struct spt_entry *);

#endif
