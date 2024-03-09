#include "tls.h"
#include <stdio.h>
#include <stderr.h>
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>  //for getpagesize()

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
  pthread_t tid;      // which thread
  unsigned int size;  // size of storage
  struct page *addr;  // storage location
};

struct page{
  unsigned int num_ref;  // number of references to a page
  unsigned int *head;    // start of the page
};

struct mapping{
  struct tls storage;   // tls struct for the matching thread
  pthread_t thread;     // id of the thread tls belongs to
  struct mapping *next; // next pointer for linked list
  struct mapping *prev; // previous pointer for linked list;
};

/*
 * Now that data structures are defined, here's a good place to declare any
 * global variables.
 */
static char init = 0;
/*
 * With global data declared, this is a good point to start defining your
 * static helper functions.
 */

/*
 * Lastly, here is a good place to add your externally-callable functions.
 */ 

int tls_create(unsigned int size)
{
  if (!init){
    //initialize sig handler
    struct sigaction handler;
    sigemptyset(&handler.sa_mask);
    handler.sa_flags = SA_SIGINFO;
    handler.sa_handler = tls_fault;
    sigaction(SIGSEGV, &handler, NULL);

    //create linked list head
    struct mapping *head = malloc(sizeof(mapping));
    head->prev = NULL;
    head->next = NULL;
    head->tid = NULL;
    head->tls = NULL
  }

  //check if thread is already mapped to 
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
