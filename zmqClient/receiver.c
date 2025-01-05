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

#define BASE_PORT 4445

void *context;
atomic_long total_bytes_received = 0;
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
    const char *directory = "/home/cc/zmqClient/data";

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

void run_blob_detection_scripts(DataQuality data_quality, int step)
{
    int status;
    if (data_quality == REDUCED)
    {
        printf("Running Reduced blob detection...\n");
        char command[512];
        snprintf(command, sizeof(command), "/home/cc/zmqClient/venv/bin/python /home/cc/zmqClient/scripts/data_to_blob_detection.py --app_name xgc --data_type reduced --path ~/zmq/data/ --output_name xgc_reduced.png --step %d > /dev/tty", step);
        status = system(command);
    }
    else
    {
        printf("Running Full blob detection...\n");
        char command[512];
        snprintf(command, sizeof(command), "/home/cc/zmqClient/venv/bin/python /home/cc/zmqClient/scripts/combine.py --step %d > /dev/tty", step);
        status = system(command);
    }
    // printf("step (%d): System status: %d, It took %.3f seconds to run the blob detection scripts\n", step, status, time_taken);
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
    gettimeofday(&start, NULL);
    while (!is_port_complete)
    {
        // Receive filename
        zmq_msg_init(&msg);
        zmq_msg_recv(&msg, socket, 0);
        size_t filename_len = zmq_msg_size(&msg);
        char *filename = malloc(filename_len + 1);
        memcpy(filename, zmq_msg_data(&msg), filename_len);
        filename[filename_len] = '\0';
        char *filepath = construct_filepath(filename, step);
        // Logging
        printf("Step (%d), Receiving file: %s\n", step, filename);

        // Add filename to appropriate array based on quality
        StepInfo *current_step = get_or_create_step(step, quality);

        // Opening file for appending
        FILE *file = fopen(filepath, "ab");

        bool is_file_complete = false;
        while (!is_file_complete)
        {
            zmq_msg_init(&msg);
            zmq_msg_recv(&msg, socket, 0);

            if (zmq_msg_size(&msg) == 0)
            {
                is_file_complete = true;
                continue;
            }

            long chunk_size = zmq_msg_size(&msg);
            char *buffer = malloc(chunk_size);
            memcpy(buffer, zmq_msg_data(&msg), chunk_size);
            fwrite(buffer, 1, chunk_size, file);
            free(buffer);
            atomic_fetch_add(&total_bytes_received, chunk_size);
        }

        printf("Step (%d) Received file: %s\n", step, filename);

        // Add filename to step
        if (thread_index == 0)
        {
            add_filename(&current_step->reduced_filenames, filename);
        }
        else
        {
            add_filename(&current_step->augmentation_filenames, filename);
        }

        // Close file
        fclose(file);
        free(filename);
        free(filepath);

        // Receive alert message
        zmq_msg_init(&msg);
        zmq_msg_recv(&msg, socket, 0);
        int alert = atoi(zmq_msg_data(&msg));

        // if 0 that means the port is complete and no more steps,
        // if 1 more files to come with the same step,
        // if 2 move to the next step by incrementing step

        // Logging And ack
        if (thread_index == 0 && alert != 1)
        {

            // Timing
            gettimeofday(&end, NULL);
            double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;
            printf("step (%d): It took %.3f seconds to receive Reduced files\n", step, elapsed);

            // Ack message
            char ack_message[256];
            snprintf(ack_message, sizeof(ack_message), "step (%d): Received Reduced files", step);
            zmq_msg_init_size(&msg, strlen(ack_message) + 1);
            memcpy(zmq_msg_data(&msg), ack_message, strlen(ack_message) + 1);
            zmq_msg_send(&msg, socket, 0);
            zmq_msg_close(&msg);
        }
        else if (thread_index == 1 && alert != 1)
        {
            // Timing
            gettimeofday(&end, NULL);
            double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;
            printf("step (%d): It took %.3f seconds to receive Augmentation files\n", step, elapsed);

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
            continue;
        case 2:
            mark_step_complete(step, thread_index == 0 ? FULL : REDUCED);
            step++;
            // Timing -> Start again
            gettimeofday(&start, NULL);
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