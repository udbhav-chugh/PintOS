#ifndef VM_PAGE
#define VM_PAGE

#include <hash.h>
#include "filesys/off_t.h"
#include "filesys/file.h"

enum spte_type{ CODE = 0, /* Swappable type code */FILE = 1, /* Executable ro file. */ MMAP = 2  /* Memory mapped files. */ };

struct spt_entry
  {
    enum spte_type type;
    void *upage;
    void *frame;  
    struct hash_elem elem;
    struct file *file;
    off_t ofs;
    bool writable;
    uint32_t page_read_bytes;
    uint32_t page_zero_bytes;
    bool pinned;
    bool is_in_swap;
    size_t idx; /* Page index in swap partition. */
  };


//Function Declarations
void supp_page_table_init (struct hash *);
struct spt_entry *uvaddr_to_spt_entry (void *);

bool stack_increase (void *,bool,void*);

bool file_supp_creation (struct file *, off_t, uint8_t *,
                       uint32_t, uint32_t, bool);
struct spt_entry* create_spte_mmap (struct file *, int, void *);

void destroy_spt (struct hash *);
void free_spte_mmap (struct spt_entry *);

bool write_to_disk (struct spt_entry *);
#endif
