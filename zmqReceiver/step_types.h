#ifndef STEP_TYPES_H
#define STEP_TYPES_H

#include <stdbool.h>

typedef enum {
    FULL,
    REDUCED
} DataQuality;

// Structure to store filenames for each step
typedef struct {
    char **filenames;
    int filename_count;
    int capacity;
} FilenameArray;

// Structure to store completion status for each quality level
typedef struct {
    bool reduced_done;
    bool augmentation_done;
} CompletionStatus;

// Structure to store step information
typedef struct {
    int step;
    FilenameArray reduced_filenames;
    FilenameArray augmentation_filenames; 
    CompletionStatus status;
} StepInfo;

// Array to store steps
typedef struct {
    StepInfo *steps;
    int count;
    int capacity;
} StepArray;

#endif // STEP_TYPES_H