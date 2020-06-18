#ifndef VM_SWAP
#define VM_SWAP

void swap_init();
size_t swap_out (struct spt_entry *);
void swap_in (struct spt_entry *);
void swap_end ();

#endif
