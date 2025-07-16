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
   /* Head and tail initial values:
    * The ring buffer's head and tail values need to be set differently
    * depending on whether the queue is initialized as empty or full.
    * In either case, all the entries have a cycle value of 0.
    * If the queue is empty, both the head and tail values are set to
    * num_entries. This translates to a cycle of 1 and index of 0.
    * If the queue is full, the head is set to 0 while the tail is set to
    * num_entries. The is equivalent to head = {cycle 0, index 0},
    * tail = {cycle 1, index 0}. So, the tail is exactly num_entries ahead
    * of head.
    */
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
   // Enqueuing a value involves updating the entry at the tail, and then
   // moving the tail one element.
   // We assume that the buffer is not full.
   uint64_t t, j;
   entry e, new_e;
   do {
      //Attempt to load the current tail.
      t = ring->tail;
      //Do some entry index remapping cache magic to reduce false sharing.
      j = cache_remap(ring_buffer_index(t, ring->num_entries), ring->num_entries);
      //Read the entry from the index indicated.
      e = ring->buf[j];
      /* If the cycle in the entry is the same as the cycle of the tail, then
       * we know that the tail value is stale; the entry has been updated, but
       * the tail has not yet been moved. This is because enqueues set the cycle
       * of each appended entry to the cycle of the tail value it used to
       * append.
       */
      if (e.cycle == ring_buffer_cycle(t, ring->num_entries)) {
         //Attempt to increment the tail on behalf of the other thread that
         //already updated the entry.
         CAS(&ring->tail, &t, t+1);
         continue;
      }
      /* If the cycle in the entry is not one behind the cycle of the tail, then
       * the tail is stale (other enqueues have moved the tail around one loop,
       * and updated the entry with a greater cycle).
       * TODO: Why do we care about this case? Wouldn't the CAS on the entry
       * fail anyways, so we would retry anyways?
       * Answer: No, the CAS would not fail. The tail became stale between the
       * load of the tail and the load of the entry. If the tail doesn't make
       * another loop after entry is loaded, the CAS will succeed, which will
       * move the cycle of the entry backwards.
       */
      if (e.cycle + 1 != ring_buffer_cycle(t, ring->num_entries)) {
         //Reload the tail value and try again.
         continue;
      }
      //Construct the new entry value.
      new_e.cycle = ring_buffer_cycle(t, ring->num_entries);
      new_e.index = val;
   } while (!CAS(&ring->buf[j].val, &e.val, new_e.val));
   CAS(&ring->tail, &t, t + 1);
}

bool ring_buffer_dequeue(ring_buffer *ring, uint32_t *val) {
   // Dequeuing a value involves reading the entry at the head, and then
   // moving the head one element.
   uint64_t h, j;
   entry e;
   do {
      //Load the head value.
      h = ring->head;
      //Do some entry index remapping cache magic to reduce false sharing.
      j = cache_remap(ring_buffer_index(h, ring->num_entries), ring->num_entries);
      //Load the entry associated with the index of the head value.
      e = ring->buf[j];
      /* We expect the entry to have the same cycle as the head value.
       * If it does not, then one of two things have happened:
       * 1. The entry cycle is exactly one less than the head cycle.
       * In this case, the entry has not yet been filled with a value,
       * which indicates the buffer is empty.
       * 2. The entry cycle is some other value. Assuming sequential consistency,
       * the entry cycle is then greater than the head cycle. This means that
       * other threads have dequeued at least n entries between the load of head
       * and the entry. The head value is stale, so we retry the loop.
       */
      if (e.cycle != ring_buffer_cycle(h, ring->num_entries)) {
         if (e.cycle + 1 == ring_buffer_cycle(h, ring->num_entries)) {
            return false;
         }
         continue;
      }
   //Once we checked the cycle matches, attempt to increment the head value.
   //If it fails, then that means some other thread has already claimed this
   //entry, so we retry the loop.
   } while (!CAS(&ring->head, &h, h+1));
   //Read out the value from the entry.
   *val = e.index;
   return true;
}
