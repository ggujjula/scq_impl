#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <pthread.h>
#include "scq.h"

scq *s;

void * thread_worker(void *arg) {
   void *test_ptr = arg;
   if (!scq_enqueue(s, test_ptr)) {
      return 2;
   }
   void *retval = scq_dequeue(s);
   printf("%p %p\n", test_ptr, retval);
   assert(test_ptr == retval);
   return 0;
}

int main() {
   unsigned num_threads = 1;
   s = scq_init(64);
   if (s == NULL) {
      return 1;
   }
   pthread_t * threads = calloc(num_threads, sizeof(pthread_t));

   for (unsigned i = 0; i < num_threads; i++) {
       int ret = pthread_create(&threads[i], NULL, &thread_worker, 0xdeadbeef + i);
       if(ret != 0) {
           printf ("Create pthread error!\n");
           return 3;
       }
   }
   for (unsigned i = 0; i < num_threads; i++) {
      void *retval;
       int ret = pthread_join(threads[i], &retval);
       if(ret != 0) {
           printf ("Join pthread error!\n");
           return 3;
       }
       if(retval != 0) {
           printf ("pthread retval value error!\n");
           return 4;
       }
   }
   printf("Done\n");
   return 0;
}
