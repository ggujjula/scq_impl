#include <stddef.h>
#include <stdlib.h>

#include "ring_buffer.h"


#define CAS(addr, expect, desired) \
    __atomic_compare_exchange_n (addr, expect, desired, \
          false /*weak*/, \
          __ATOMIC_SEQ_CST /* success_memorder */, \
          __ATOMIC_SEQ_CST /* failure_memorder */)

typedef union entry {
   uint64_t val;
   struct __attribute__((packed)) {
      uint32_t cycle;
      uint32_t index;
   };
} entry;

typedef struct ring_buffer {
   uint64_t num_entries;
   uint64_t head;
   uint64_t tail;
   entry *buf;
} ring_buffer;

ring_buffer *ring_buffer_init(uint64_t num_entries, bool full) {
   ring_buffer *ring = calloc(1, sizeof(ring_buffer));
   if (ring == NULL) {
      return NULL;
   }
   ring->buf = calloc(num_entries, sizeof(entry));
   if (ring->buf == NULL) {
      free(ring);
      return NULL;
   }
   ring->num_entries = num_entries;
   ring->head = full ? 0 : num_entries;
   ring->tail = num_entries;
   return ring;
}

/* Stripe the entry indicies so that cache lines are not used for adjacent
 * entries.
 * I think this increases cache miss rate if all entries do not fit in cache.
 * The paper says this reduces false sharing. I guess the tradeoff is worth it.
 */
static uint64_t cache_remap(uint64_t index, uint64_t num_entries) {
   uint64_t cache_size = 64;
   uint64_t entry_size = sizeof(entry);
   uint64_t entries_per_line = cache_size / entry_size;
   uint64_t lines = num_entries / entries_per_line;
   return (entries_per_line * (index % lines)) + (index / lines);
}

void ring_buffer_enqueue(ring_buffer *ring, uint32_t val) {
   uint64_t t, j;
   entry e, new_e;
   do {
      t = ring->tail;
      j = cache_remap(ring_buffer_index(t, ring->num_entries), ring->num_entries);
      e = ring->buf[j];
      if (e.cycle == ring_buffer_cycle(t, ring->num_entries)) {
         CAS(&ring->tail, &t, t+1);
         continue;
      }
      if (e.cycle + 1 != ring_buffer_cycle(t, ring->num_entries)) {
         continue;
      }
      new_e.cycle = ring_buffer_cycle(t, ring->num_entries);
      new_e.index = val;
   } while (!CAS(&ring->buf[j].val, &e.val, new_e.val));
   CAS(&ring->tail, &t, t + 1);
}

bool ring_buffer_dequeue(ring_buffer *ring, uint32_t *val) {
   uint64_t h, j;
   entry e;
   do {
      h = ring->head;
      j = cache_remap(ring_buffer_index(h, ring->num_entries), ring->num_entries);
      e = ring->buf[j];
      if (e.cycle != ring_buffer_cycle(h, ring->num_entries)) {
         if (e.cycle + 1 == ring_buffer_cycle(h, ring->num_entries)) {
            return false;
         }
         continue;
      }
   } while (!CAS(&ring->head, &h, h+1));
   *val = e.index;
   return true;
}
