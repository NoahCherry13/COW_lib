#include "tls.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>  //for getpagesize()
#include <stddef.h>
#include <pthread.h>

#define MAX_THREADS 128

/*
 * This is a good place to define any data structures you will use in this file.
 * For example:
 *  - struct TLS: may indicate information about a thread's local storage
 *    (which thread, how much storage, where is the storage in memory)
 *  - struct page: May indicate a shareable unit of memory (we specified in
 *    homework prompt that you don't need to offer fine-grain cloning and CoW,
 *    and that page granularity is sufficient). Relevant information for sharing
 *    could be: where is the shared page's data, and how many threads are sharing it
 *  - Some kind of data structure to help find a TLS, searching by thread ID.
 *    E.g., a list of thread IDs and their related TLS structs, or a hash table.
 */

struct tls{
  pthread_t tid;          // which thread
  unsigned int size;      // size of storage
  unsigned int page_count; // number pages allocated
  struct page ** page_addr;      // storage location
};

struct page{
  unsigned int ref_count;  // number of references to a page
  unsigned int *page_head;    // start of the page
};

struct mapping{
  struct tls *tls;       // tls struct for the matching thread
  pthread_t tid;         // id of the thread tls belongs to
};




/*
 * Now that data structures are defined, here's a good place to declare any
 * global variables.
 */
static char init = 0;
static struct mapping thread_dict[MAX_THREADS];
static int tls_count;
unsigned int ps; //page size




/*
 * With global data declared, this is a good point to start defining your
 * static helper functions.
 */
unsigned int byte_to_page(int bytes){
  unsigned int pages = (bytes + ps/2)/ps;
  return pages;
}

void tls_fault(int sig, siginfo_t *si, void *context){
  
  unsigned long p_fault = ((unsigned long) si->si_addr) & ~(ps-1); 
  //struct page *page_ind;
  struct tls *tls_ptr;

  for (int i = 0; i < MAX_THREADS; i++){
    if (thread_dict[i].tid != (pthread_t) -1){
      tls_ptr = thread_dict[i].tls;
      for (int j = 0; j < tls_ptr->page_count; j++){
	if ((unsigned long)tls_ptr->page_addr[j]->page_head == p_fault){
	  pthread_exit(NULL);
	}
      }
    }
  }
  signal(SIGSEGV, SIG_DFL);
  signal(SIGBUS, SIG_DFL);
  raise(sig);
}

void tls_init(){
  ps = getpagesize();
  // init the sig handler
  struct sigaction handler;
  sigemptyset(&handler.sa_mask);
  handler.sa_flags = SA_SIGINFO;
  handler.sa_sigaction = tls_fault;
  sigaction(SIGSEGV, &handler, NULL);
  sigaction(SIGBUS, &handler, NULL);

  for (int i = 0; i < MAX_THREADS; i++){
    thread_dict[i].tls = NULL;
    thread_dict[i].tid = (unsigned long int)-1;
  }
}

int get_key(pthread_t tid){
  for (int i = 0; i < MAX_THREADS; i++){
    if (tid == thread_dict[i].tid){
      return i;
    }
  }
  return -1;
}

int find_free_tls()
{
  for (int i = 0; i < MAX_THREADS; i++){
    if (thread_dict[i].tid == -1){
      return i;
    }
  }
  return -1;
}




/*
 * Lastly, here is a good place to add your externally-callable functions.
 */ 

int tls_create(unsigned int size)
{
  
  if (!init){
    tls_init();
    init = 1;
  }

  int key = get_key(pthread_self());
  int new_entry = -1;
  int pages;
  struct tls* tls_ptr;


  if (key != -1){
    if (thread_dict[key].tls->size > 0){
      printf("LSA Already Exists!\n");
      return -1;
    }
    tls_ptr = thread_dict[key].tls;
  }
  else {
    new_entry = find_free_tls();
    if (new_entry == -1){
      printf("No Empty Entries!\n");
      return -1;
    }

    tls_ptr = malloc(sizeof(struct tls));
    thread_dict[new_entry].tls = tls_ptr;
    thread_dict[new_entry].tid = pthread_self();
    
    tls_ptr->tid = pthread_self();
    tls_ptr->size = 0;
    tls_ptr->page_count = 0;
    tls_ptr->page_addr = NULL;
  }

  pages = size / ps;
  pages += (size % ps > 0);
  tls_ptr->size = pages * ps;
  
  if (pages){
    tls_ptr->page_addr = malloc(pages * sizeof(struct page));
    for (int i = 0; i < pages; i++){
      tls_ptr->page_addr[i] = malloc(sizeof(struct page));
      tls_ptr->page_addr[i] = mmap(0, ps, PROT_NONE, MAP_ANON | MAP_PRIVATE, 0, 0);
      tls_ptr->page_addr[i]->ref_count = 1;
    }
    tls_ptr->page_count = pages;
  }
  tls_count++;
  return 0;
}



int tls_destroy()
{

  struct mapping *map_ind = head;
  struct page *page_ind;
  struct page *np;
  int current_thread = pthread_self();
  while (map_ind->next != NULL){
    if (map_ind->tid == current_thread){
      break;
    }
    map_ind = map_ind->next;
  }
  
  //--------------Handle Error Cases-------------------//
  if (map_ind->tid != current_thread){
    printf("No TLS Entry for Current Thread\n");
    return -1;
  }

  page_ind = map_ind->tls->addr;
  // traverse pages and free unreferenced
  for(int i = 0; i < map_ind->tls->num_pages-1; i++){
    np = page_ind->next_page;
    page_ind->num_ref--;
    if(page_ind->num_ref <= 0){
      if (munmap((void *) page_ind->head, ps)){
	printf("Unmapping Failed! Exiting...\n");
	return(-1);
      }
      free(page_ind);
      page_ind = np;
    }
  }
  //free last page
  if(page_ind->num_ref <= 0){
    if (munmap((void *) page_ind->head, ps)){
      printf("Unmapping Failed! Exiting...\n");
      return(-1);
    }
    free(page_ind);
  }

  //free tls
  free(map_ind->tls);

  //patch holes in linked list
  map_ind = head;
  if(head->tid == current_thread){
    head = head->next;
    free(map_ind);
  }else{
    while(map_ind->next != NULL){
      if(map_ind->next->tid == current_thread){
	map_ind->next = map_ind->next->next;
      }
    }
  }
  
  return 0;
}
/*
int tls_read(unsigned int offset, unsigned int length, char *buffer)
{
  struct mapping *map_ind = head;
  struct page *page_ind;
  int pages_loaded = (int)((offset + length)/ps)+(offset&&1); //number of pages to load
  int current_thread = pthread_self();
  int page_offset = offset % ps;
  //int start_page = offset / ps;
  int bytes_read = 0;
  int is_first_page = 1;
  //find mapping for current thread

  if(head == NULL) return -1;

  while (map_ind->next != NULL){
    if (map_ind->tid == current_thread){
      break;
    }
    map_ind = map_ind->next;
  }

  //--------------Handle Error Cases-------------------//
  if (map_ind->tid != current_thread){
    printf("No TLS Entry for Current Thread\n");
    return -1;
  }

  if (offset + length > map_ind->tls->size){
    printf("Buffer OOB for TLS");
    return -1;
  }
  
  page_ind = map_ind->tls->addr;
  //----------------loop to find start page ind---------//
  for(int i = 1; i < page_offset; i++){
    page_ind = page_ind->next_page;
  }

  //--------------Mem Unprotect and read-----------------//
  // memcpy -> dest, src, size
  // loop to unprotect one page at a time to read from and read length in
  for(int i = 0; i < pages_loaded; i++){
    // try to unprotect current page
    if(mprotect((void *)page_ind->head, ps, PROT_READ | PROT_WRITE)){
      printf("Unable to Unprotect Page\n");
      exit(0);
    }
    
    //do stuff here
    
    if(is_first_page && offset + length > ps){
      // in first page and need to read over page boundary
      is_first_page = !is_first_page;
      memcpy(buffer + bytes_read, page_ind->head + offset, ps - (offset + length));
      bytes_read += ps - (offset + length);
    }else if(length - bytes_read > ps){
      // not in first page and need to read full page
      memcpy(buffer + bytes_read, page_ind->head + offset, ps);
      bytes_read += ps;
    }else{
      // in final page -- read rest of length from current page
      memcpy(buffer + bytes_read, page_ind->head + offset, length - bytes_read);
    }
    if (mprotect((void*) page_ind->head, ps, PROT_NONE)) {
      printf("Unable to Reprotect Page\n");
      exit(0);
    }
  }
    
  return 0;
}

int tls_write(unsigned int offset, unsigned int length, const char *buffer)
{
  struct mapping *map_ind = head;
  struct page *page_ind;
  int pages_loaded = (int)((offset + length)/ps)+(offset&&1); //number of pages to load
  int current_thread = pthread_self();
  int page_offset = offset % ps;
  //int start_page = offset / ps;
  int bytes_written = 0;
  int is_first_page = 1;
  //find mapping for current thread
  while (map_ind->next != NULL){
    if (map_ind->tid == current_thread){
      break;
    }
    map_ind = map_ind->next;
  }
  
  //--------------Handle Error Cases-------------------//
  if (map_ind->tid != current_thread){
    printf("No TLS Entry for Current Thread\n");
    return -1;
  }
  
  if (offset + length > map_ind->tls->size){
    printf("Buffer OOB for TLS");
    return -1;
  }
  page_ind = map_ind->tls->addr;
  //----------------loop to find start page ind---------//
  for(int i = 1; i < page_offset; i++){
    page_ind = page_ind->next_page;
  }
  
  //--------------Mem Unprotect and read-----------------//
  // memcpy -> dest, src, size
  // loop to unprotect one page at a time to read from and read length in
  for(int i = 0; i < pages_loaded; i++){
    // try to unprotect current page
    if(mprotect((void *)page_ind->head, ps, PROT_READ | PROT_WRITE)){
      printf("Unable to Unprotect Page\n");
      exit(0);
    }
    
    //do stuff here
    if(page_ind->num_ref > 1){
      // tls is already referenced by another thread      
      void *temp_page = mmap(0, ps, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, 0, 0);
      page_ind->num_ref = 1;
      memcpy(temp_page, page_ind->head, ps);
      page_ind = temp_page;
    }

    
    if(is_first_page && offset + length > ps){
      // in first page and need to read over page boundary
      is_first_page = !is_first_page;
      memcpy(page_ind->head + page_offset, buffer + bytes_written, ps-offset);
    }else if(length - bytes_written > ps){
      // not in first page and need to read full page
      memcpy(page_ind->head, buffer + bytes_written, ps);
    }else{
      // in final page -- read rest of length from current page
      memcpy(page_ind->head, buffer + bytes_written, length-bytes_written);
    }
    if (mprotect((void*) page_ind->head, ps, PROT_NONE)) {
      printf("Unable to Reprotect Page\n");
      exit(0);
    }
  }
  
  return 0;
}

int tls_clone(pthread_t tid)
{
  int current_thread = pthread_self();
  struct mapping *map_ind = head;
  struct mapping *tid_thread = head;
  //-----------Find Current Thread---------------------//
  while (map_ind->next != NULL){
    if (map_ind->tid == current_thread){
      printf("LSA Exists for Current Thread\n");
      return -1;
    }
    map_ind = map_ind->next;
  }
  //----------Find Thread Specified by TID-------------//
  while (tid_thread->next != NULL){
    if (tid_thread->tid == tid){
      break;
    }
    tid_thread = tid_thread->next;
  }
  
  //--------------Handle Error Cases-------------------//
  if (tid_thread->tid != tid){
    printf("No LSA for Specified Thread\n");
    return -1;
  }
  
  map_ind->next = (struct mapping *)malloc(sizeof(struct mapping));
  map_ind = map_ind->next;
  map_ind->tid = pthread_self();
  map_ind->tls = (struct tls *)malloc(sizeof(struct tls));
  map_ind->tls->num_pages = tid_thread->tls->num_pages;
  map_ind->tls->addr = tid_thread->tls->addr;
  map_ind->tls->size = tid_thread->tls->size;

  //increment ref count for all copied pages
  struct page *page_ind = tid_thread->tls->addr;
  for(int i = 0; i < tid_thread->tls->num_pages; i++){
    page_ind->num_ref++;
    page_ind = page_ind->next_page;
  }
  
  return 0;
}
*/
