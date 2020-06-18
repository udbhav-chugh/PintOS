#include "threads/synch.h"
#include "devices/disk.h"
#include <bitmap.h>
#include "vm/page.h"
#include "userprog/process.h"

static struct disk *swap_disk = NULL;
/* Lock acquired whenever the swap table is accessed, no lock is required while 
   using swap partition disk functions as it internally synchronizes accesses. */
static struct lock swap_lock;
static struct bitmap *swap_table = NULL;
static uint32_t swap_table_size = 0;

/* Initializes swap table bitmap. */
void
swap_init()
{
  swap_disk = disk_get (1,1);
  lock_init (&swap_lock);
  if (swap_disk != NULL){
    swap_table_size = disk_size (swap_disk) / SECTORS_PER_PAGE;
    swap_table = bitmap_create (swap_table_size);
  }
}

/* Fetches an empty slot, for the given read-only memory frame, 
   it puts it into swap partition and returns index which can help
   swap_in the frame. */
size_t
swap_out (struct spt_entry *spte)
{
  if (swap_table != NULL)
  {
    lock_acquire (&swap_lock);
    size_t idx = bitmap_scan_and_flip (swap_table, 0, 1, false);
    if (idx != BITMAP_ERROR)
    {
      int i;
      for (i = 0; i<SECTORS_PER_PAGE; i++)
      {
        lock_acquire (&file_lock);
        disk_write (swap_disk, (idx * SECTORS_PER_PAGE) + i,
                    spte->frame + (i * DISK_SECTOR_SIZE));
        lock_release (&file_lock);
      }
    }
    lock_release (&swap_lock);
    return idx;
  }
  return BITMAP_ERROR;
}

/* Gets a frame from allocator for the spte and loads the page from 
   SWAP partition to memory. */
void
swap_in (struct spt_entry *spte)
{
  if (swap_table != NULL)
  {
    lock_acquire (&swap_lock);
    size_t idx = spte->idx;
    int i;
    for (i = 0; i<SECTORS_PER_PAGE; i++)
    {
      lock_acquire (&file_lock);
      disk_read (swap_disk, (idx * SECTORS_PER_PAGE) + i,
                 spte->frame + (i * DISK_SECTOR_SIZE));
      lock_release (&file_lock);
    }
    bitmap_reset (swap_table, idx);
    lock_release (&swap_lock);
  }
}

void
swap_end ()
{
  if (swap_table != NULL)
  {
    lock_acquire (&swap_lock);
    bitmap_destroy (swap_table);
    lock_release (&swap_lock);
  }
}
