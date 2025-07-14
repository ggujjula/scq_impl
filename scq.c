#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/param.h>
#include "scq.h"
#include "ring_buffer.h"

typedef struct scq {
   uint64_t num_entries;
   ring_buffer *fq;
   ring_buffer *aq;
   void **data;
} scq;

scq* scq_init(size_t num_entries) {
   if (num_entries > 64) {
      return NULL;
   }
   num_entries = 64;
   scq *s = malloc(sizeof(scq));
   if (s == NULL) {
      return NULL;
   }
   s->num_entries = num_entries;
   s->fq = ring_buffer_init(num_entries, true /*full*/);
   if (s->fq == NULL) {
      free(s);
      return NULL;
   }
   s->aq = ring_buffer_init(num_entries, false /*full*/);
   if (s->aq == NULL) {
      free(s->fq);
      free(s);
      return NULL;
   }
   s->data = malloc(sizeof(void *) * 64);
   if (s->data == NULL) {
      free(s->fq);
      free(s->aq);
      free(s);
      return NULL;
   }
   return s;
}

bool scq_enqueue(scq *s, void *ptr){
   uint32_t index;
   if (!ring_buffer_dequeue(s->fq, &index)) {
      return false;
   }
   s->data[index] = ptr;
   ring_buffer_enqueue(s->aq, index);
   return true;
}

void* scq_dequeue(scq *s) {
   uint32_t index;
   void *ptr;
   if (!ring_buffer_dequeue(s->aq, &index)) {
      return NULL;
   }
   ptr = s->data[index];
   ring_buffer_enqueue(s->fq, index);
   return ptr;
}
