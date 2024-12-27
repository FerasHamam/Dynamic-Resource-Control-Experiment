#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdatomic.h>
#include "step_manager.h"

#define INITIAL_CAPACITY 10

// Global variables
StepArray step_array = {NULL, 0, 0};
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
atomic_int current_processing_step = 0;

void init_filename_array(FilenameArray *arr)
{
    arr->capacity = INITIAL_CAPACITY;
    arr->filename_count = 0;
    arr->filenames = malloc(sizeof(char *) * arr->capacity);
}

void add_filename(FilenameArray *arr, const char *filename)
{
    if (arr->filename_count >= arr->capacity)
    {
        arr->capacity *= 2;
        arr->filenames = realloc(arr->filenames, sizeof(char *) * arr->capacity);
    }
    arr->filenames[arr->filename_count] = strdup(filename);
    arr->filename_count++;
}

void init_step_array()
{
    step_array.capacity = INITIAL_CAPACITY;
    step_array.count = 0;
    step_array.steps = malloc(sizeof(StepInfo) * step_array.capacity);
}

StepInfo *get_or_create_step(int step, DataQuality quality)
{
    pthread_mutex_lock(&mutex);

    // Find existing step
    for (int i = 0; i < step_array.count; i++)
    {
        if (step_array.steps[i].step == step)
        {
            pthread_mutex_unlock(&mutex);
            return &step_array.steps[i];
        }
    }

    // Create new step if not found
    if (step_array.count >= step_array.capacity)
    {
        step_array.capacity *= 2;
        step_array.steps = realloc(step_array.steps, sizeof(StepInfo) * step_array.capacity);
    }

    StepInfo *new_step = &step_array.steps[step_array.count];
    new_step->step = step;
    new_step->isProcessed = false;
    new_step->status.reduced_done = false;
    new_step->status.augmentation_done = (quality == REDUCED ? true : false);
    init_filename_array(&new_step->reduced_filenames);
    init_filename_array(&new_step->augmentation_filenames);
    step_array.count++;

    pthread_mutex_unlock(&mutex);
    return new_step;
}

bool is_step_complete(StepInfo *step)
{
    return step->status.reduced_done && step->status.augmentation_done;
}

// Declare external function that will be defined in main.c
extern void run_blob_detection_scripts(DataQuality quality, int step);
extern void *context;

void *step_processor_thread(void *arg)
{
    while (1)
    {
        if (context == NULL)
            break;

        pthread_mutex_lock(&mutex);
        int current_step = atomic_load(&current_processing_step);

        // Exit the loop if current_step is -1
        if (current_step == -1)
        {
            pthread_mutex_unlock(&mutex);
            break;
        }

        // Find the step we're currently processing
        StepInfo *step_info = NULL;
        for (int i = 0; i < step_array.count; i++)
        {
            if (step_array.steps[i].step == current_step)
            {
                step_info = &step_array.steps[i];
                break;
            }
        }

        if (step_info && !step_info->isProcessed && is_step_complete(step_info))
        {
            printf("Processing step %d (Reduced files: %d, Augmentation files: %d)\n",
                   current_step,
                   step_info->reduced_filenames.filename_count,
                   step_info->augmentation_filenames.filename_count);

            // Process both quality levels
            if (step_info->augmentation_filenames.filename_count > 0)
            {
                run_blob_detection_scripts(FULL, current_step);
            }
            else
            {
                run_blob_detection_scripts(REDUCED, current_step);
            }

            step_info->isProcessed = true;
            // Move to next step
            atomic_fetch_add(&current_processing_step, 1);
        }
        pthread_mutex_unlock(&mutex);
        sleep(1); // Avoid busy waiting
    }
    pthread_exit(NULL);
    return NULL;
}

void mark_step_complete(int step, bool is_augmentation)
{
    pthread_mutex_lock(&mutex);
    for (int i = 0; i < step_array.count; i++)
    {
        if (step_array.steps[i].step == step)
        {
            if (is_augmentation)
            {
                step_array.steps[i].status.augmentation_done = true;
            }
            else
            {
                step_array.steps[i].status.reduced_done = true;
            }
            pthread_cond_signal(&cond);
            break;
        }
    }
    pthread_mutex_unlock(&mutex);
}

void cleanup_step_array()
{
    for (int i = 0; i < step_array.count; i++)
    {
        for (int j = 0; j < step_array.steps[i].reduced_filenames.filename_count; j++)
        {
            free(step_array.steps[i].reduced_filenames.filenames[j]);
        }
        free(step_array.steps[i].reduced_filenames.filenames);

        for (int j = 0; j < step_array.steps[i].augmentation_filenames.filename_count; j++)
        {
            free(step_array.steps[i].augmentation_filenames.filenames[j]);
        }
        free(step_array.steps[i].augmentation_filenames.filenames);
    }
    free(step_array.steps);
}