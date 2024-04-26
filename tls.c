#include "tls.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>  //for getpagesize()
#include <stddef.h>
#include <pthread.h>

#define MAX_THREADS 512

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

int check_tls_fault(unsigned long fault_address){
  
  struct tls *tls_ptr;

  for (int i = 0; i < MAX_THREADS; i++){
    if (thread_dict[i].tid != (pthread_t) -1){
      tls_ptr = thread_dict[i].tls;
      for (int j = 0; j < tls_ptr->page_count; j++){
	if ((unsigned long)tls_ptr->page_addr[j]->page_head == fault_address){
	  return 1;
	}
      }
    }
  }
  return 0;
}

void handle_fault(int sig, siginfo_t *si, void *context){
  unsigned long p_fault = ((unsigned long) si->si_addr) & ~(ps-1);
  if(check_tls_fault(p_fault)){
    pthread_exit(NULL);
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
  handler.sa_sigaction = handle_fault;
  sigaction(SIGSEGV, &handler, NULL);
  sigaction(SIGBUS, &handler, NULL);

  for (int i = 0; i < MAX_THREADS; i++){
    thread_dict[i].tls = NULL;
    thread_dict[i].tid = (unsigned long int)-1;
  }
}

int get_key(pthread_t tid){
  for (int i = 0; i < MAX_THREADS; i++){
    if (tid == -1){
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

int search_tid(pthread_t tid)
{
  for (int i = 0; i < MAX_THREADS; i++){
    if (thread_dict[i].tid == tid){
      return i;
    }
  }
  return -1;
}

void reset_entry(int ind){
  tls_count--;
  thread_dict[ind].tid = -1;
  thread_dict[ind].tls = NULL;
}

void tls_unprotect(struct page *page_ptr){
  if (mprotect((void*) page_ptr->page_head, ps, PROT_READ | PROT_WRITE)) {
    printf("Failed to unprot page\n");
    exit(1);
  }
}

void tls_protect(struct page *page_ptr){

  // Check if it was able to successfully protect the page
  if (mprotect((void*) page_ptr->page_head, ps, PROT_NONE)) {
    printf("Failed to unprot page\n");
    exit(1);
  }
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
  pages = 0;
  pages = size / ps;
  pages += ((size % ps) > 0);
  tls_ptr->size = pages * ps;
  if (pages){
    tls_ptr->page_addr = malloc(pages * sizeof(struct page));
    for (int i = 0; i < pages; i++){
      tls_ptr->page_addr[i] = malloc(sizeof(struct page));
      tls_ptr->page_addr[i]->page_head = mmap(0, ps, PROT_NONE, MAP_ANON | MAP_PRIVATE, 0, 0);
      tls_ptr->page_addr[i]->ref_count = 1;
    }
   
    tls_ptr->page_count = pages;
  }
  //printf("Pages: %d\n", pages);
  tls_count++;
  return 0;
}



int tls_destroy()
{
  int tid_ind = search_tid(pthread_self());
  struct page **page_addr;
  struct tls *tls_ptr;

  // check if tls entry exists to be deleted
  if (tid_ind == -1){
    printf("No TLS Entry to Destroy\n");
    return -1;
  }
  // reference pointers
  tls_ptr = thread_dict[tid_ind].tls;
  page_addr = thread_dict[tid_ind].tls->page_addr;

  // free unref pages
  for (int i = 0; i < tls_ptr->page_count; i++){
    page_addr[i]->ref_count--;

    //check if page ref
   
    if (!page_addr[i]->ref_count){
      munmap((void *) page_addr[i]->page_head, ps);
      free(page_addr[i]);
     
    }
    //free(page_addr);
   
  }
  free(tls_ptr);
  reset_entry(tid_ind);
 
  return 0;
}


int tls_read(unsigned int offset, unsigned int length, char *buffer)
{
  int tid_ind = search_tid(pthread_self());
  // check if tls entry exists to be read
  if (tid_ind == -1){
    printf("No TLS Entry to Read\n");
    return -1;
  }

  int start_page = offset / ps;
  int start_page_offset = offset % ps;
  int offset_flag = ((offset + length) % ps > 0);
  int pages_to_read = (length + start_page_offset) / ps + offset_flag;
  int bytes_read = 0;
  int bytes_left = length;
  int current_read;
  struct page **page_addr;
  struct tls *tls_ptr;
   
  // reference pointers
  tls_ptr = thread_dict[tid_ind].tls;
  page_addr = tls_ptr->page_addr;

  if (tls_ptr->size < (length + offset)){
    printf("Read too big for TLS\n");
    return -1;
  }
  
  for (int i = 0; i < pages_to_read; i++){
    tls_unprotect(page_addr[i + start_page]);
    if (bytes_left + start_page_offset >= ps){
      current_read = ps - start_page_offset;
    } else {
      current_read = bytes_left;
    }

    memcpy(buffer + bytes_read, (char *)page_addr[i + start_page]->page_head + start_page_offset, current_read);
    start_page_offset = 0;
    bytes_read += current_read;
    bytes_left -= current_read;
    tls_protect(page_addr[i + start_page]);
  }
  
  return 0;
}


int tls_write(unsigned int offset, unsigned int length, const char *buffer)
{
  int tid_ind = search_tid(pthread_self());
  // check if tls entry exists to be read
  if (tid_ind == -1){
    printf("No TLS Entry to Read\n");
    return -1;
  }
  
  int start_page = offset / ps;
  int start_page_offset = offset % ps;
  int offset_flag = ((offset + length) % ps > 0);
  int pages_to_write = (length + start_page_offset) / ps + offset_flag;
  int bytes_written = 0;
  int bytes_left = length;
  int current_write;
  struct page **page_addr;
  struct tls *tls_ptr;
 
  // reference pointers
  tls_ptr = thread_dict[tid_ind].tls;
  page_addr = tls_ptr->page_addr;

  if (tls_ptr->size < (length + offset)){
    printf("Write too big for TLS\n");
    return -1;
  }
  
  for (int i = 0; i < pages_to_write; i++){
    tls_unprotect(page_addr[i + start_page]);
    if(page_addr[i + start_page]->ref_count > 1){
      void *memptr = mmap(0, ps, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, 0, 0);
      memcpy(memptr, page_addr[i + start_page]->page_head, ps);
      tls_protect(page_addr[i + start_page]);

      //for(int i = 0; i < length; i++) printf("%c",((char*)memptr)[i]);
      //printf("\n");
      page_addr[i + start_page] = malloc(sizeof(struct page));
      page_addr[i + start_page]->page_head = memptr;
      page_addr[i + start_page]->ref_count = 1;
    }
    if (bytes_left + start_page_offset >= ps){
      current_write = ps - start_page_offset;
    } else {
      current_write = bytes_left;
    }

    memcpy(page_addr[i + start_page]->page_head + start_page_offset, buffer + bytes_written, current_write);
    bytes_written += current_write;
    bytes_left -= current_write;
    start_page_offset = 0;
    tls_protect(page_addr[i + start_page]);
  }
  return 0;
}

int tls_clone (pthread_t tid){

  
  int clone_key = search_tid(tid);
  int thread_key = search_tid(pthread_self());
  struct tls *clone_tls;
  struct tls *thread_tls;
  // check if tid has a dictionary entry
  
  if (clone_key == -1){
    printf("No TLS Entry for Specified Thread\n");
    return -1;
  }

  // check if current thread has a dictionary entry
  if (thread_key != -1){
    printf("Existing Entry for Running Thread!\n");
    return -1;
  }
  
  thread_key = find_free_tls();
  clone_tls = thread_dict[clone_key].tls;
  thread_tls = malloc(sizeof(struct tls));
  thread_dict[thread_key].tls = thread_tls;
  thread_dict[thread_key].tid = pthread_self();

  thread_tls->page_count = clone_tls->page_count;
  thread_tls->tid = pthread_self();
  thread_tls->size = clone_tls->size;
  thread_tls->page_addr = NULL;
  
  if(clone_tls->page_count > 0){
    thread_tls->page_addr = malloc(clone_tls->page_count * sizeof(struct page)); 
    for (int i = 0; i < clone_tls->page_count; i++){
      thread_tls->page_addr[i] = clone_tls->page_addr[i];
      thread_tls->page_addr[i]->ref_count++;
      
    }
  }
  tls_count++;
  return 0;
}
