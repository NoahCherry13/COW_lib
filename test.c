#include "tls.h"
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>


// Function for thread 1: Create TLS, write and read to it, wait then read from it again
void* test(void* arg){
  if (tls_read(0, 7, "hello!")) printf("bruh\n");
  if (tls_create(2 * getpagesize())) printf("bruh\n");
  if (tls_write(0, 7, "hello!")) printf("bruh\n");
  char m[7];
  if (tls_read(0, 7, m)) printf("bruh\n");
  printf("thread 1: %s\n", m);

  sleep(2);

  if (tls_read(0, 7, m)) printf("bruh\n");
  printf("thread 1: %s\n", m);
  if (tls_destroy()) printf("bruh\n");
  return NULL;
}

// Function for thread 2: Clone TLS from thread 1, read from it, then write and read again
void* test2(void* arg){
  sleep(1);
  pthread_t * pid = arg;
  if (tls_clone(*pid)) printf("bruh\n");
  char m[7];
  if (tls_read(0, 7, m)) printf("bruh\n");
  printf("thread 2: %s\n", m);

  if (tls_write(getpagesize()+1, 7, "shello")) printf("bruh\n");
  if (tls_read(getpagesize()+1, 7, m)) printf("bruh\n");
  printf("thread 2: %s\n", m);
  if (tls_read(0, 7, m)) printf("bruh\n");
  printf("thread 2: %s\n", m);
  if (tls_destroy()) printf("bruh\n");

  return NULL;
}

// Main function creates 2 threads then sleeps
/*
void *test(){
  //int count = 0;
  for (int i = 0; i < 10; i++){
    printf("creating tls\n");
    tls_create(1);
    printf("destroying tls\n");
    tls_destroy();
  }
  return NULL;
}
*/

int main(){
  pthread_t tid;
  pthread_t tid2;
  printf("creating 2\n");
  if (pthread_create(&tid2, NULL, test2, &tid) != 0){
    perror("Error: ");
  }
  
  if (pthread_create(&tid, NULL, test, NULL) != 0){
    perror("Error: ");
  }
  
  
  //  sleep(5);

}


