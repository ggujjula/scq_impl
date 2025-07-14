#include <stdint.h>
#include <stdbool.h>

typedef struct ring_buffer ring_buffer;

ring_buffer *ring_buffer_init(uint64_t num_entries, bool full);
static uint64_t ring_buffer_index(uint64_t ptr, uint64_t num_entries) {
   return ptr % num_entries;
}

static uint64_t ring_buffer_cycle(uint64_t ptr, uint64_t num_entries) {
   return ptr / num_entries;
}

void ring_buffer_enqueue(ring_buffer *ring, uint32_t val);
bool ring_buffer_dequeue(ring_buffer *ring, uint32_t *val);
