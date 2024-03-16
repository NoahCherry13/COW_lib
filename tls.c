#include "tls.h"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>  //for getpagesize()
#include <stddef.h>
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
  struct page *addr;      // storage location
};

struct page{
  unsigned int num_ref;  // number of references to a page
  unsigned int *head;    // start of the page
  struct page *next_page;
  struct page *prev_page;
};

struct mapping{
  struct tls *tls;       // tls struct for the matching thread
  pthread_t tid;         // id of the thread tls belongs to
  struct mapping *next;  // next pointer for linked list
  struct mapping *prev;  // previous pointer for linked list;
};

/*
 * Now that data structures are defined, here's a good place to declare any
 * global variables.
 */
static char init = 0;
static struct mapping *head;
/*
 * With global data declared, this is a good point to start defining your
 * static helper functions.
 */

unsigned int byte_to_page(int bytes){
  
  unsigned int pages = (bytes + getpagesize()/2)/getpagesize();
  return pages;
}

void tls_fault(int sig){

}

/*
 * Lastly, here is a good place to add your externally-callable functions.
 */ 

int tls_create(unsigned int size)
{
  if (!init){
    head = malloc(sizeof(struct mapping));
    //initialize sig handler
    struct sigaction handler;
    sigemptyset(&handler.sa_mask);
    handler.sa_flags = SA_SIGINFO;
    handler.sa_handler = tls_fault;
    sigaction(SIGSEGV, &handler, NULL);

    //create linked list head
    struct mapping *head = malloc(sizeof(struct mapping));
    head->prev = NULL;
    head->next = NULL;
  }

  //check if thread is already mapped to tls 
  pthread_t tid = pthread_self();
  struct mapping *map_ind = head;

  while(map_ind->next != NULL){
    map_ind = map_ind->next;
    if (map_ind->tid == tid && map_ind->tls->size){
      printf("tls already assigned to thread\n{tid: %ld}\n{size: %d}", tid, map_ind->tls->size);
      return -1;
    }
  }

  //thread is not mapped to tls

  //create new mapping and initialize values
  struct mapping *new_map = malloc(sizeof(struct mapping));
  new_map->tid = tid;
  new_map->tls = malloc(sizeof(struct tls));
  new_map->tls->size = size;
  new_map->tls->num_pages = byte_to_page(size);
  

  int num_pages = byte_to_page(size);
  if (size > 0){
    //create head of list
    new_map->tls->addr = (struct page *) malloc(num_pages);
    struct page *ref_page = new_map->tls->addr;
    ref_page->head = mmap(0, getpagesize(), PROT_NONE, MAP_ANON | MAP_PRIVATE, 0, 0);
    //populate rest of list as needed;
    for (int i = 1; i < num_pages; i++){
      ref_page->next_page = (struct page *) malloc(num_pages);
      ref_page = ref_page->next_page;
      ref_page->head = mmap(0, getpagesize(), PROT_NONE, MAP_ANON | MAP_PRIVATE, 0, 0);
      //****************ADD CASE FOR WHEN MMAP FAILS****************//
    }
  }
  

  return 0;
}

int tls_destroy()
{
  
  return 0;
}

int tls_read(unsigned int offset, unsigned int length, char *buffer)
{
	return 0;
}

int tls_write(unsigned int offset, unsigned int length, const char *buffer)
{
	return 0;
}

int tls_clone(pthread_t tid)
{
	return 0;
}
