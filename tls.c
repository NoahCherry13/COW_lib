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
#define TLS_FREE -1
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
  unsigned int num_pages; // number pages allocated
  struct page **page_list;      // storage location
};

struct page{
  unsigned int num_ref;  // number of references to a page
  unsigned int *addr;    // start of the page
};

struct mapping{
  struct tls *tls;       // tls struct for the matching thread
  pthread_t tid;         // id of the thread tls belongs to
  // struct mapping *next;  // next pointer for linked list
  // struct mapping *prev;  // previous pointer for linked list;
};

/*
 * Now that data structures are defined, here's a good place to declare any
 * global variables.
 */
static char init = 0;
static struct mapping map_list[MAX_THREADS];
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
 
 
  for(int i = 0; i < MAX_THREADS; i++){
    if(!(map_list[i].tid+1)) continue;
    for(int j = 0; j < map_list[i].tls->num_pages; j++){
      if((unsigned long) map_list[i].tls->page_list[j] == p_fault){
	pthread_exit(NULL);
      }
    }
  }
  signal(SIGSEGV, SIG_DFL);
  signal(SIGBUS, SIG_DFL);
  raise(sig);
}

int tls_unprot(struct page *pg)
{
  if (mprotect((void *) pg->addr, ps, PROT_READ|PROT_WRITE)){
    printf("Failed to Unprotect Page\n");
    return -1;
  }
  return 0;
}

int tls_prot(struct page *pg)
{
  if (mprotect((void *) pg->addr, ps, PROT_NONE)){
    printf("Failed to Protect Page\n");
    return -1;
  }
  return 0;
}

int tls_search(pthread_t tid)
{
  for (int i = 0; i < MAX_THREADS; i++){
    if((int)map_list[i].tid == tid){
      return i;
      printf("found thread!\n");
    }
  }
  return -1;
}

void tls_init()
{
  for(int i = 0; i < MAX_THREADS; i++){
    map_list[i].tid = TLS_FREE;
    map_list[i].tls = NULL;
  }
  //initialize sig handler
    struct sigaction handler;
    sigemptyset(&handler.sa_mask);
    handler.sa_flags = SA_SIGINFO;
    handler.sa_sigaction = tls_fault;
    sigaction(SIGSEGV, &handler, NULL);
    sigaction(SIGBUS, &handler, NULL);
}

/*
 * Lastly, here is a good place to add your externally-callable functions.
 */ 

int tls_create(unsigned int size)
{
  ps = getpagesize();
  if (!init){
    init = 1;
    tls_init();
  }
  
  //check if thread is already mapped to tls 
  pthread_t tid = pthread_self();
  int map_ind = tls_search(tid);
  if(map_ind+1){
    if(map_list[map_ind].tls->size){
      printf("NON-ZERO TLS EXISTS FOR THREAD\n");
      return -1;
    }
  }

  //thread is not mapped to tls
  
  //find empty ind
  int new_ind = -1;
  for(int i = 0; i < MAX_THREADS; i++){
    if(map_list[i].tid == -1){
      new_ind = i;
      break;
    }
  }
  if(new_ind == -1){
    printf("No TLS entries available\n");
    return -1;
  }

  int num_pages = byte_to_page(size);
  map_list[new_ind].tid = tid;
  map_list[new_ind].tls = malloc(sizeof(struct tls));
  map_list[new_ind].tls->size = size;
  map_list[new_ind].tls->num_pages = num_pages;
  printf("new_ind: %d\n", new_ind);

  if (size){
    map_list[new_ind].tls->page_list = (struct page **) calloc(num_pages, sizeof(struct page));
    for(int i = 0; i < num_pages; i++){
      map_list[new_ind].tls->page_list[i]->num_ref = 1;
      map_list[new_ind].tls->page_list[i] = malloc(sizeof(struct page));
      map_list[new_ind].tls->page_list[i] = mmap(0, ps,  PROT_NONE, MAP_ANON|MAP_PRIVATE, 0, 0);
    }
  }

  return 0;
}

int tls_destroy()
{
  int current_thread = pthread_self(); 
  int ct_ind = tls_search(current_thread);
  //--------------Handle Error Cases-------------------//
  if(!(ct_ind+1)){
    printf("Specied Thread Has No TLS Entry\n");
    return -1;
  }

  
  // traverse pages and free unreferenced
  for(int i = 0; i < map_list[ct_ind].tls->num_pages; i++){
    map_list[ct_ind].tls->page_list[i]->num_ref--;
    if(map_list[ct_ind].tls->page_list[i]->num_ref <= 0){
      munmap((void *)map_list[ct_ind].tls->page_list[i]->addr, ps);
    }
  }

  //free tls
  free(map_list[ct_ind].tls->page_list);
  free(map_list[ct_ind].tls);    
  map_list[ct_ind].tls = NULL;
  map_list[ct_ind].tid = -1;
  
  return 0;
}

int tls_read(unsigned int offset, unsigned int length, char *buffer)
{
  ps = getpagesize();
  int pages_loaded = (int)((offset + length)/ps)+(offset&&1); //number of pages to load
  int page_offset = offset % ps;
  int start_page = offset / ps;
  int bytes_read = 0;
  int is_first_page = 1;

  //find mapping for current thread
  pthread_t tid = pthread_self();
  int map_ind = tls_search(tid);
  

  //--------------Handle Error Cases-------------------//
  if ((!map_ind+1)){
    printf("No TLS Entry for Current Thread\n");
    return -1;
  }

  if (offset + length > map_list[map_ind].tls->size){
    printf("Buffer OOB for TLS");
    return -1;
  }
  
  //--------------Mem Unprotect and read-----------------//
  // memcpy -> dest, src, size
  // loop to unprotect one page at a time to read from and read length in
  for(int i = start_page; i < start_page + pages_loaded; i++){
    // try to unprotect current page
    if(mprotect((void *)map_list[map_ind].tls->page_list[i], ps, PROT_READ | PROT_WRITE)){
      printf("Unable to Unprotect Page\n");
      exit(0);
    }
    
    //do stuff here
    
    if(is_first_page && page_offset + length > ps){
      // in first page and need to read over page boundary
      is_first_page = !is_first_page;
      memcpy(buffer + bytes_read, map_list[map_ind].tls->page_list[i]->addr + page_offset, ps - (page_offset + length));
      bytes_read += ps - (offset + length);
    }else if(length - bytes_read > ps){
      // not in first page and need to read full page
      memcpy(buffer + bytes_read, map_list[map_ind].tls->page_list[i]->addr, ps);
      bytes_read += ps;
    }else{
      // in final page -- read rest of length from current page
      memcpy(buffer + bytes_read, map_list[map_ind].tls->page_list[i]->addr, length - bytes_read);
    }
    if (mprotect((void*) map_list[map_ind].tls->page_list[i], ps, PROT_NONE)) {
      printf("Unable to Reprotect Page\n");
      exit(0);
    }
  }
    
  return 0;
}

int tls_write(unsigned int offset, unsigned int length, const char *buffer)
{
  ps = getpagesize();
  int pages_loaded = (int)((offset + length)/ps)+(offset&&1); //number of pages to load
  int page_offset = offset % ps;
  int start_page = offset / ps;
  int bytes_read = 0;
  int is_first_page = 1;
  void *temp_mem;


  //find mapping for current thread
  pthread_t tid = pthread_self();
  int map_ind = tls_search(tid);

//--------------Handle Error Cases-------------------//
  if ((!map_ind+1)){
    printf("No TLS Entry for Current Thread\n");
    return -1;
  }

  if (offset + length > map_list[map_ind].tls->size){
    printf("Buffer OOB for TLS");
    return -1;
  }
  
  //--------------Mem Unprotect and write-----------------//
  for(int i = start_page; i < start_page + pages_loaded; i ++){

    // Check if CoW
    if(map_list[map_ind].tls->page_list[i]->num_ref > 1){
      memcpy(temp_mem, (void*) map_list[map_ind].tls->page_list[i]->addr, ps);
      map_list[map_ind].tls->page_list[i] = malloc(sizeof(struct page));
      map_list[map_ind].tls->page_list[i]->num_ref = 1;
      map_list[map_ind].tls->page_list[i]->addr = temp_mem;
    }
    tls_unprot(map_list[map_ind].tls->page_list[i]);


    //-------------Write to Page--------------------------//
    if(is_first_page && offset + length > ps)
      {
	// in first page and need to read over page boundary
	is_first_page = !is_first_page;
	memcpy((char *)(map_list[map_ind].tls->page_list[i]->addr + page_offset), buffer+bytes_read,  ps - (page_offset + length));
	bytes_read += ps - (page_offset + length);
      }
    else if(length - bytes_read > ps)
      {
	// not in first page and need to read full page
	memcpy((char*)(buffer + bytes_read), map_list[map_ind].tls->page_list[i]->addr, ps);
	bytes_read += ps;
      }
    else
      {
	// in final page -- read rest of length from current page
	memcpy((char*)(buffer + bytes_read), map_list[map_ind].tls->page_list[i]->addr, length - bytes_read);
      }

    // reprotect page
    if (mprotect((void*) map_list[map_ind].tls->page_list[i], ps, PROT_NONE)) {
      printf("Unable to Reprotect Page\n");
      exit(0);
    }
  }
  return 0;
}



int tls_clone(pthread_t tid)
{
  int map_ind = tls_search(pthread_self());
  int tid_ind = tls_search(tid);
  int new_ind = -1;
  if((map_ind+1)){
    if(map_list[map_ind].tls->size > 0){
      printf("TLS Entry Exists for Current Thread\n");
      return -1;
    }
  }
  
  if(!(map_ind+1)){
    printf("No TLS Entry for Specified Thread\n");
    return -1;
  }
  
  for (int i = 0; i < MAX_THREADS; i++){
    if(map_list[i].tid == -1){
      new_ind = i;
      break;
    }
  }

  map_list[new_ind].tls = (struct tls *)malloc(sizeof(struct tls));
  map_list[new_ind].tls->size = map_list[tid_ind].tls->size;
  map_list[new_ind].tls->num_pages = map_list[tid_ind].tls->num_pages;

  if(map_list[new_ind].tls->num_pages){
    map_list[new_ind].tls->page_list = (struct page **)calloc(map_list[new_ind].tls->num_pages, sizeof(struct page *));
    for(int i = 0; i < map_list[new_ind].tls->num_pages; i++){
      map_list[new_ind].tls->page_list[i] = map_list[tid_ind].tls->page_list[i];
      map_list[new_ind].tls->page_list[i]->num_ref++;
    }
  }
 
  return 0;
}
