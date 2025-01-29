#ifndef STEP_MANAGER_H
#define STEP_MANAGER_H

#include "step_types.h"
#include <pthread.h>
#include <stdatomic.h>
#include <sys/time.h>


void init_filename_array(FilenameArray *arr);
void add_filename(FilenameArray *arr, const char *filename);
void init_step_array();
StepInfo* get_or_create_step(int step, DataQuality quality);
void mark_step_complete(int step, bool is_augmentation);
void *step_processor_thread(void *arg);
void cleanup_step_array();

// External variables that need to be accessible
extern StepArray step_array;
extern pthread_mutex_t mutex;
extern pthread_cond_t cond;
extern atomic_int current_processing_step;

#endif // STEP_MANAGER_H