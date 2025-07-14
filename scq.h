#include <stdlib.h>
#include <stdbool.h>

int scq_test();

typedef struct scq scq;

scq* scq_init(size_t num_entries);

bool scq_enqueue(scq *, void *);
void* scq_dequeue(scq *);
