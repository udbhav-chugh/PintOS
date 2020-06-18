#include "threads/synch.h"
#include "threads/palloc.h"
#include "vm/frame.h"
#include "vm/page.h"
#include <malloc.h>
#include <list.h>
#include "userprog/pagedir.h"
#include <bitmap.h>
#include "vm/swap.h"

//Declaration of locks and lists to be used
static struct list frame_table;
static struct lock frame_table_lock;

//Function declarations
static void clear_frame_entry (struct frame_table_entry *);
static void *frame_alloc (enum palloc_flags, void* AUX);
bool evict_frame (struct frame_table_entry *);

//Initialising frame table lock and list
void frame_table_init (void)
{
  list_init (&frame_table);
  lock_init (&frame_table_lock);
}

/* Unoptimized enhanced second-chance page replacement. 
   Called with frame_table_lock in aquired state. */
static struct frame_table_entry *
get_victim_frame ()
{
  ASSERT (lock_held_by_current_thread (&frame_table_lock));
  struct list_elem *e;
  //Use dirty and access bits to replace frames for eviction
  for (e = list_begin (&frame_table);
       e != list_end (&frame_table);
       e = list_next (e))
  {
    struct frame_table_entry *fte =
      list_entry (e, struct frame_table_entry, elem);
    bool is_dirty = pagedir_is_dirty (fte->t->pagedir,
                                      fte->spte->upage);
    bool is_accessed = pagedir_is_accessed (fte->t->pagedir,
                                            fte->spte->upage);

    if (!fte->spte->pinned)
    {
      if (fte->spte->type != CODE)
      {
        if (is_dirty)
        {
          if (write_to_disk (fte->spte))
            pagedir_set_dirty (fte->t->pagedir, fte->spte->upage, false);
        }
        else if (!is_accessed)
          return fte;
      }
      else
      {
        if (!is_dirty && !is_accessed)
          return fte;
      }
    }
  }
  for (e = list_begin (&frame_table);
       e != list_end (&frame_table);
       e = list_next (e))
  {
    struct frame_table_entry *fte =
      list_entry (e, struct frame_table_entry, elem);
    bool is_dirty = pagedir_is_dirty (fte->t->pagedir,
                                      fte->spte->upage);
    bool is_accessed = pagedir_is_accessed (fte->t->pagedir,
                                            fte->spte->upage);

    if (!fte->spte->pinned)
    {
      if ((!is_dirty ) && !is_accessed)
        return fte;
      else //Accessed or (Dirty (FILE or MMAP)).
        pagedir_set_accessed (fte->t->pagedir, fte->spte->upage, false);
    }
  }

  ASSERT (!list_empty (&frame_table));
  for (e = list_begin (&frame_table);
       e != list_end (&frame_table);
       e = list_next (e))
  {
    struct frame_table_entry *fte =
      list_entry (e, struct frame_table_entry, elem);
    if (!fte->spte->pinned){
      return fte;
    }
  }
  return NULL;
}

bool
evict_frame (struct frame_table_entry *fte)
{
  ASSERT (lock_held_by_current_thread (&frame_table_lock));
  struct spt_entry *spte = fte->spte;
  size_t idx;
  switch (spte->type){
  case MMAP:

    /* Given frame to evict of type FILE or MMAP will 
       never be dirty. */

    if (pagedir_is_dirty (fte->t->pagedir, spte->upage))
        if (!write_to_disk (spte))
        {
          PANIC ("Not able to write out");
          return false;
        }

    spte->frame = NULL;
    
    clear_frame_entry (fte);
    return true;
    break;
  case FILE:
    spte->type = CODE;
  case CODE:
    ASSERT (spte->frame != NULL);
    idx = swap_out (spte);
    if (idx == BITMAP_ERROR){
      PANIC ("Not able to swap out");
      return false;
    }

    spte->idx = idx;
    spte->is_in_swap = true;
    spte->frame = NULL;

    clear_frame_entry (fte);
    return true;
    break;
  default:
    PANIC ("Corrupt fte or spte");
    return false;
  }
  return true;
}

//Helper for frame table entry filling
void fill_table_details(struct frame_table_entry *fte, void *frame, struct spt_entry *spte)
{
  fte->t = thread_current ();
  fte->spte = spte;
  ASSERT (fte->spte->type < 3 && fte->spte->type >= 0);
  fte->frame = frame;
}

//Helper function for allocating frame in which page would be loaded
void * retrieve_frame_of_page (enum palloc_flags flags, struct spt_entry *spte)
{
  if(spte == NULL)
    return NULL;
  if (flags & PAL_USER == 0) return NULL;
  void *frame = frame_alloc (flags, NULL);
  //Edge cases checks
  if (frame != NULL)
  {
    frame_table_add (frame, spte);
    return frame;
  }
  else PANIC ("Not able to get frame");
}

//Add the supplementary page table entry to the frame table
static void frame_table_add (void *frame, struct spt_entry *spte)
{
  struct frame_table_entry *fte =(struct frame_table_entry *) malloc (sizeof (struct frame_table_entry));
  //Acquire frame table lock and fill the details of entry 
  lock_acquire (&frame_table_lock);
  fill_table_details(fte,frame,spte);
  list_push_back (&frame_table, &fte->elem);
  lock_release (&frame_table_lock);
}

//For a kernel page taken from user pool, locate kernel virtual address
static void *
frame_alloc (enum palloc_flags flags,void * AUX)
{
  if (flags & PAL_USER == 0)
    return NULL;

  void *frame = palloc_get_page (flags);
  if (frame != NULL)
    return frame;
  else
  {
    lock_acquire (&frame_table_lock);
    do {
      if (list_empty (&frame_table))
        PANIC ("palloc_get_page returned NULL when frame table empty.");

      struct frame_table_entry *fte = get_victim_frame ();

      /* Always get some frame to evict. */
      ASSERT (fte != NULL);

      /* Check not corrupt fte or spte. */
      /*printf ("\nhere::%p, %s, %p, %d, %p, %p", fte->t,
        fte->t->name, fte->spte, fte->spte->type,
        fte->spte->frame, fte->spte->upage);*/
      ASSERT (fte->spte->type < 3 && fte->spte->type >= 0 &&
              fte->frame != NULL);
      ASSERT (fte->spte->frame != NULL);

      bool evicted = evict_frame (fte);
      if (evicted){
        frame = palloc_get_page (flags);
      }
      else
        PANIC ("Not able to evict. ");
    } while (frame == NULL);
      
    lock_release (&frame_table_lock);
    return frame;
  }
}

//Deallocate the frame to free up memory
void free_frame (void *frame)
{
  struct frame_table_entry *fte;
  struct list_elem *e;
  lock_acquire (&frame_table_lock);
  for (e = list_begin (&frame_table);
       e != list_end (&frame_table);
       e = list_next (e))
  {
    fte = list_entry (e, struct frame_table_entry, elem);
    if (fte->frame == frame)
    {
      list_remove (&fte->elem);
      break;
    }
  }
  lock_release(&frame_table_lock);
  palloc_free_page (frame);
}

static void
clear_frame_entry (struct frame_table_entry *fte)
{
  ASSERT (lock_held_by_current_thread (&frame_table_lock));
  list_remove (&fte->elem);
  pagedir_clear_page (fte->t->pagedir, fte->spte->upage);
  palloc_free_page (fte->frame);
  free (fte);
}
