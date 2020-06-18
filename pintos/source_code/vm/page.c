#include "vm/page.h"
#include <malloc.h>
#include <bitmap.h>
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "userprog/process.h"
#include "filesys/file.h"

//Function declarations
static struct spt_entry* create_spte ();
static bool install_load_file (struct spt_entry *);
static bool install_load_mmap (struct spt_entry *);
static bool install_load_swap (struct spt_entry *);
static void free_spte_elem (struct hash_elem *, void *);
static void free_spte (struct spt_entry *);

//Parent function for loading page according to the function, i.e files,mmap or swap
bool install_load_page (struct spt_entry *spte)
{
  if (spte->type == FILE)
    return install_load_file (spte);
  else if (spte->type == MMAP)
    return install_load_mmap (spte);
  else if (spte->type == CODE)
    return install_load_swap (spte);
  else
    return false;
}


//Helper for spte details
void spte_details(struct spt_entry *spte,void *upage,struct file *f,int ofs,uint32_t page_zero_bytes,uint32_t page_read_bytes)
{
    spte->upage = upage;
    spte->file = f;
    spte->ofs = ofs;
    spte->writable = true;
    spte->page_zero_bytes = page_zero_bytes;
    spte->page_read_bytes = page_read_bytes;
}
//Comparator for user page
bool cmp_spt (const struct hash_elem * a1, const struct hash_elem *a2, void *aux UNUSED)
{
  struct spt_entry *first = hash_entry (a1, struct spt_entry, elem);
  struct spt_entry *second = hash_entry (a2, struct spt_entry, elem);
  return (int) first->upage < (int) second->upage;
}

//Hash function for supplementary table
unsigned supp_hashing (const struct hash_elem *element, void *aux UNUSED)
{
  struct spt_entry *spte = hash_entry (element, struct spt_entry, elem);
  return hash_int ((int) spte->upage);
}

//Initialize supplementary page table
void supp_page_table_init (struct hash *supp_page_table)
{
  hash_init (supp_page_table, supp_hashing, cmp_spt, NULL);
}

//Create spte entry for code
struct spt_entry *create_spte_code (void *upage)
{
  struct spt_entry *spte = create_spte ();
  spte->upage = upage;
  spte->type = CODE;
  hash_insert (&((thread_current())->supp_page_table), &spte->elem);
  return spte;
}

//Create spte entry for memory mapped files
struct spt_entry *create_spte_mmap (struct file *f, int read_bytes, void *upage)
{
  struct thread *t = thread_current();
  uint32_t page_read_bytes, page_zero_bytes;
  int ofs = 0;
  int i = 0;
  struct spt_entry *first_spte = NULL;
  
  while (read_bytes > 0)
  {
    page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
    page_zero_bytes = PGSIZE - page_read_bytes;

    struct spt_entry *spte = uvaddr_to_spt_entry (upage);
    if (spte != NULL){
      free_spte_mmap (first_spte);
      return NULL;
    }
    spte = create_spte ();
    spte->type = MMAP;
    spte_details(spte,upage,f,ofs,page_zero_bytes,page_read_bytes);
    ofs += page_read_bytes;
    read_bytes -= page_read_bytes;
    upage += PGSIZE;
    
    hash_insert (&(t->supp_page_table), &spte->elem);
    if (i == 0)
    {
      first_spte = spte;
      i++;
    }
    
  }
  return first_spte;
}

//Map user virtual address to spte entry
struct spt_entry * uvaddr_to_spt_entry (void *uvaddr)
{
  void *upage = pg_round_down (uvaddr);
  struct spt_entry spte;
  spte.upage = upage;

  struct hash_elem *e = hash_find (&thread_current()->supp_page_table, &spte.elem);

  if (!e)return NULL;
  else return hash_entry (e, struct spt_entry, elem);
}

//Dynamically create a spte entry
static struct spt_entry *create_spte ()
{
  struct spt_entry *spte = (struct spt_entry *) malloc (sizeof (struct spt_entry));
  spte->upage = NULL;
  spte->frame = NULL;
  spte->is_in_swap = false;
  spte->idx = BITMAP_ERROR;
  spte->pinned = false;
  return spte;
}

//Create the spte entries and populate the table
bool file_supp_creation (struct file *file, off_t ofs, uint8_t *upage, uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT (ofs % PGSIZE == 0&&pg_ofs (upage) == 0&&(read_bytes + zero_bytes) % PGSIZE == 0);
  while ( zero_bytes > 0 || read_bytes > 0) 
  {
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;
      struct spt_entry *spte = create_spte ();
      spte->type = FILE;
      spte_details(spte,upage,file,ofs,page_zero_bytes,page_read_bytes);
      spte->writable = writable;
      ofs += page_read_bytes;
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
      hash_insert (&((thread_current())->supp_page_table), &spte->elem);
  }
  return true;
}

//Helper for loading page for memory mapped files
static bool install_load_mmap (struct spt_entry *spte)
{
  return install_load_file (spte);
}

//Load the page for swap table
static bool install_load_swap (struct spt_entry *spte)
{
  void *frame = retrieve_frame_of_page (PAL_USER | PAL_ZERO, spte);
  ASSERT (frame != NULL);

  if (frame == NULL) return false;
  if (install_page (spte->upage, frame, true))
  {
    spte->frame = frame;
    if (spte->is_in_swap)
    {
      swap_in (spte);
      spte->is_in_swap = false;
      spte->idx = BITMAP_ERROR;
    }
    return true;
  }
  else free_frame (frame);
  return false;
}

//Aids in unmapping the file from supp. table
void free_spte_mmap (struct spt_entry *first_spte)
{
  if (first_spte != NULL)
  {
    int read_bytes = file_length (first_spte->file);
    void *upage = first_spte->upage;
    struct spt_entry *spte;
    while (read_bytes > 0)
    {
      spte = uvaddr_to_spt_entry (upage);
      upage += PGSIZE;
      read_bytes -= spte->page_read_bytes;
      if (spte->file == first_spte->file) free_spte (spte);
    }
  }
}

//Write file back to disk if the page is dirty
bool write_to_disk (struct spt_entry *spte)
{
  struct thread *t = thread_current ();
  if (pagedir_is_dirty (t->pagedir, spte->upage))
  {
    lock_acquire (&file_lock);
    off_t written = file_write_at (spte->file, spte->upage, spte->page_read_bytes, spte->ofs);
    lock_release (&file_lock);
    if (written != spte->page_read_bytes) return false;
  }
  return true;
}

//Write back to disk based on dirty bit
static void free_spte (struct spt_entry *spte)
{
  if (spte != NULL)
  {
    if (spte->frame != NULL)
    {
      if(spte->type == MMAP || (spte->type == FILE && spte->writable)) write_to_disk (spte);
      void *pd = thread_current()->pagedir;
      pagedir_clear_page (pd, spte->upage);
      free_frame (spte->frame);
    }
    hash_delete (&thread_current()->supp_page_table,&spte->elem);
    free (spte);
  }
}

//Helper for loading page for file type
static bool install_load_file (struct spt_entry *spte)
{
  void *frame = retrieve_frame_of_page (PAL_USER, spte);
  ASSERT (frame != NULL);
  if (frame == NULL) return false;

  lock_acquire (&file_lock);
  file_seek (spte->file, spte->ofs);
  int read_bytes = file_read (spte->file, frame, spte->page_read_bytes);
  lock_release (&file_lock);
  if (read_bytes != (int) spte->page_read_bytes)
  {
    free_frame (frame);
    return false; 
  }
  memset (frame + spte->page_read_bytes, 0, spte->page_zero_bytes);
  if (!install_page (spte->upage, frame, spte->writable)) 
  {
    free_frame (frame);
    return false; 
  }
  spte->frame = frame;
  return true;
}

//Free hash table entry
static void free_spte_elem (struct hash_elem *e, void *aux)
{
  struct spt_entry *spte = hash_entry (e, struct spt_entry, elem);
  free_spte (spte);
}

//Remove the hash entry from supp. table
void destroy_spt (struct hash *supp_page_table)
{
  hash_destroy (supp_page_table, free_spte_elem);
}

//If the stack doesn't exceed max_stack size, we allow the stack to grow
bool stack_increase (void *uaddr,bool pinned,void* AUX)
{
  void *upage = pg_round_down (uaddr);
  if ((size_t) (PHYS_BASE - uaddr) > MAX_STACK_SIZE) return false;
  struct spt_entry *spte = create_spte_code (upage);
  spte->pinned = pinned;
  return install_load_page (spte);
}

