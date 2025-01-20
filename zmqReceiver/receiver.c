#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zmq.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include "step_manager.h"

#define BASE_PORT 4444

void *context;
DataQuality shared_data_quality = FULL;

// Function to create directories recursively
int create_directories(const char *path)
{
    char temp[256];
    char *p = NULL;

    // Copy the path to a temporary buffer
    snprintf(temp, sizeof(temp), "%s", path);

    // Iterate through each component of the path
    for (p = temp + 1; *p; p++)
    {
        if (*p == '/')
        {
            *p = '\0'; // Temporarily terminate the string
            if (mkdir(temp, S_IRWXU) != 0 && errno != EEXIST)
            {
                perror("mkdir");
                return -1; // Directory creation failed
            }
            *p = '/'; // Restore the slash
        }
    }

    // Create the final directory
    if (mkdir(temp, S_IRWXU) != 0 && errno != EEXIST)
    {
        perror("mkdir");
        return -1;
    }

    return 0; // Success
}

char *construct_filepath(const char *filename, int step)
{
    char *filepath = (char *)malloc(256 * sizeof(char));
    if (filepath == NULL)
    {
        perror("Failed to allocate memory");
        return NULL;
    }

    char new_directory[256];
    const char *directory = "../data";

    if (strstr(filename, "reduced") != NULL)
    {
        snprintf(new_directory, sizeof(new_directory), "%s/%s/%d/", directory, "reduced", step);
    }
    else if (strstr(filename, "delta") != NULL)
    {
        snprintf(new_directory, sizeof(new_directory), "%s/%s/%d/", directory, "delta", step);
    }
    else
    {
        snprintf(new_directory, sizeof(new_directory), "%s", directory);
    }

    // Ensure the directory exists
    if (create_directories(new_directory) != 0)
    {
        free(filepath);
        return NULL;
    }

    // Construct the full file path
    sprintf(filepath, "%s%s", new_directory, filename);
    return filepath;
}

void log_time_info(struct timeval *start, double *bytesReceived, int type)
{
    struct timeval end;
    gettimeofday(&end, NULL);

    // Check if 2 seconds have passed
    double elapsed = (end.tv_sec - start->tv_sec) + (end.tv_usec - start->tv_usec) / 1000000.0;
    if (elapsed >= 2.0)
    {
        // Determine file path based on data type (reduced - 0, full - 1)
        const char *filePath = (type == 0) ? "../data/time_reduced.txt" : "../data/time_aug.txt";
        FILE *file = fopen(filePath, "a");
        if (file == NULL)
        {
            perror("Error opening file");
            return;
        }
        char buffer[128];
        int written = snprintf(buffer, sizeof(buffer), "%.3f, %.3f\n", elapsed, *bytesReceived);
        if (written < 0)
        {
            perror("Error formatting data");
            fclose(file);
            return;
        }
        if (fwrite(buffer, 1, strlen(buffer), file) != strlen(buffer))
        {
            perror("Error writing to file");
        }
        fclose(file);
        printf("Logged: Elapsed = %.3f, BytesReceived = %.3f to %s\n", elapsed, *bytesReceived, filePath);
        gettimeofday(start, NULL);
        *bytesReceived = 0.0;
    }
}

void run_blob_detection_scripts(DataQuality data_quality, int step)
{
    int status;
    return;
    if (data_quality == REDUCED)
    {
        printf("Running Reduced blob detection...\n");
        char command[512];
        snprintf(command, sizeof(command), "../venv/bin/python ../scripts/data_to_blob_detection.py --app_name xgc --data_type reduced --path ~/zmq/data/ --output_name xgc_reduced.png --step %d > /dev/tty", step);
        status = system(command);
    }
    else
    {
        printf("Running Full blob detection...\n");
        char command[512];
        snprintf(command, sizeof(command), "../venv/bin/python ../scripts/combine.py --step %d > /dev/tty", step);
        status = system(command);
    }
    printf("step (%d): System status: %d, run the blob detection scripts\n", step, status);
}

void *recv_data(void *arg)
{
    int thread_index = *(int *)arg;
    free(arg);
    int step = 0;

    // Initialize the socket
    char bind_address[50];
    int port = BASE_PORT + thread_index;
    void *socket = zmq_socket(context, ZMQ_PAIR);
    snprintf(bind_address, sizeof(bind_address), "tcp://0.0.0.0:%d", port);
    zmq_bind(socket, bind_address);
    printf("Binding to port %d\n", port);

    DataQuality quality = shared_data_quality;

    zmq_msg_t msg;
    bool is_port_complete = false;

    // Timing
    struct timeval start, end;
    double bytes_received = 0.0;
    while (!is_port_complete)
    {
        // Receive filenames
        char **filenames = NULL;
        char **filepaths = NULL;
        int file_count = 0;
        // Add filename to appropriate array based on quality
        StepInfo *current_step = get_or_create_step(step, quality);
        printf("Step (%d) Started\n", step);
        while (true)
        {
            zmq_msg_init(&msg);
            zmq_msg_recv(&msg, socket, 0);
            size_t filename_len = zmq_msg_size(&msg);
            if (filename_len == 0)
            {
                break; // End of filenames
            }

            filenames = realloc(filenames, (file_count + 1) * sizeof(char *));
            filepaths = realloc(filepaths, (file_count + 1) * sizeof(char *));
            if (filenames == NULL || filepaths == NULL)
            {
                perror("Failed to allocate memory");
                exit(EXIT_FAILURE);
            }

            filenames[file_count] = malloc(filename_len + 1);
            memcpy(filenames[file_count], zmq_msg_data(&msg), filename_len);
            filenames[file_count][filename_len] = '\0';
            filepaths[file_count] = construct_filepath(filenames[file_count], step);
            add_filename(thread_index == 0 ? &current_step->reduced_filenames : &current_step->augmentation_filenames, filenames[file_count]);
            file_count++;
        }
        // Open files for writing
        FILE *files[file_count];
        for (int i = 0; i < file_count; i++)
        {
            files[i] = fopen(filepaths[i], "ab");
            if (files[i] == NULL)
            {
                perror("Failed to open file");
                exit(EXIT_FAILURE);
            }
        }

        // Receive files chunks
        int num_read_files = 0;
        int file_index = 0;
        int iter = 0;
        while (num_read_files < file_count)
        {
            // Receive file chunks
            gettimeofday(&start, NULL);
            zmq_msg_init(&msg);
            zmq_msg_recv(&msg, socket, 0);
            gettimeofday(&end, NULL);
            size_t chunk_size = zmq_msg_size(&msg);
            if (chunk_size == 0)
            {
                printf("Step (%d), Received file: %s\n", step, filenames[file_index]);
                num_read_files++;
                iter = 0;
                fclose(files[file_index]);
                file_index = (file_index + 1) % file_count;
                continue;
            }
            char *buffer = malloc(chunk_size);
            memcpy(buffer, zmq_msg_data(&msg), chunk_size);
            fwrite(buffer, 1, chunk_size, files[file_index]);
            free(buffer);
            // Logging Timing each 2 seconds
            bytes_received += chunk_size;
            // log_time_info(&start, &bytes_received, thread_index == 0 ? 0 : 1);
            double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;
            // Send elapsed time to log
            zmq_msg_init_data(&msg, &elapsed, sizeof(double), NULL, NULL);
            zmq_msg_send(&msg, socket, 0);
            iter++;
            if (iter % 10 == 0)
            {
                iter = 0;
                file_index = (file_index + 1) % file_count;
            }
        }

        // Receive alert message
        zmq_msg_init(&msg);
        zmq_msg_recv(&msg, socket, 0);
        int alert = atoi(zmq_msg_data(&msg));

        // if 0 that means the port is complete and no more steps,
        // if 1 move to the next step by incrementing step

        // Logging And ack
        if (thread_index == 0)
        {
            printf("step (%d): received Reduced files\n", step);
            // Ack message
            char ack_message[256];
            snprintf(ack_message, sizeof(ack_message), "step (%d): Received Reduced files", step);
            zmq_msg_init_size(&msg, strlen(ack_message) + 1);
            memcpy(zmq_msg_data(&msg), ack_message, strlen(ack_message) + 1);
            zmq_msg_send(&msg, socket, 0);
            zmq_msg_close(&msg);
        }
        else if (thread_index == 1)
        {
            printf("step (%d): received Augmentation files\n", step);
            // Ack message
            char ack_message[256];
            snprintf(ack_message, sizeof(ack_message), "step (%d): Received Augmentation files", step);
            zmq_msg_init_size(&msg, strlen(ack_message) + 1);
            memcpy(zmq_msg_data(&msg), ack_message, strlen(ack_message) + 1);
            zmq_msg_send(&msg, socket, 0);
            zmq_msg_close(&msg);
        }

        // Process alert
        switch (alert)
        {
        case 1:
            mark_step_complete(step, thread_index == 0 ? FULL : REDUCED);
            step++;
            break;
        default:
            mark_step_complete(step, thread_index == 0 ? FULL : REDUCED);
            is_port_complete = true;
            break;
        }
    }

    // Mark all steps as complete
    get_or_create_step(-1, quality);

    // Cleanup
    zmq_msg_close(&msg);
    zmq_close(socket);
    pthread_exit(NULL);
    return NULL;
}

int main()
{
    printf("Starting Receiver...\n");
    context = zmq_ctx_new();
    init_step_array();

    pthread_t high_quality_thread, low_quality_thread, processor_thread;

    pthread_create(&processor_thread, NULL, step_processor_thread, NULL);

    // Always create thread for index 0
    int *thread_index_hq = malloc(sizeof(int));
    *thread_index_hq = 0;
    pthread_create(&high_quality_thread, NULL, recv_data, thread_index_hq);

    // Create thread for index 1 (it will exit early if in REDUCED mode)
    int *thread_index_lq = malloc(sizeof(int));
    *thread_index_lq = 1;
    pthread_create(&low_quality_thread, NULL, recv_data, thread_index_lq);

    pthread_join(high_quality_thread, NULL);
    pthread_join(low_quality_thread, NULL);
    pthread_join(processor_thread, NULL);

    printf("All threads completed.\n");
    zmq_ctx_destroy(&context);

    cleanup_step_array();

    return 0;
}