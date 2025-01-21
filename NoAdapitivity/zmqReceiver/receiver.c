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
#include <pthread.h>

#define BASE_PORT 4444
#define DIRECTORY "../data/"

void *context;

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
    snprintf(new_directory, sizeof(new_directory), "%s/%d/", DIRECTORY, step);
    if (create_directories(new_directory) != 0)
    {
        free(filepath);
        return NULL;
    }
    sprintf(filepath, "%s%s", new_directory, filename);
    return filepath;
}

void run_blob_detection_scripts(int step)
{
    int status;
    return;
    printf("Running Full blob detection...\n");
    char command[512];
    snprintf(command, sizeof(command), "../venv/bin/python ../scripts/combine.py --step %d > /dev/tty", step);
    status = system(command);
    printf("step (%d): System status: %d, run the blob detection scripts\n", step, status);
}

void connect_socket(void **socket)
{
    char bind_address[50];
    snprintf(bind_address, sizeof(bind_address), "tcp://0.0.0.0:%d", BASE_PORT);
    *socket = zmq_socket(context, ZMQ_PAIR);
    zmq_bind(*socket, bind_address);
    printf("Binding to port %d\n", BASE_PORT);
}

void recv_data_chunk(void *socket, char **data, size_t *size)
{
    zmq_msg_t msg;
    zmq_msg_init(&msg);
    zmq_msg_recv(&msg, socket, 0);
    *size = zmq_msg_size(&msg);
    *data = malloc(*size);
    memcpy(*data, zmq_msg_data(&msg), *size);
    zmq_msg_close(&msg);
}

void send_data_chunk(void *socket, char *message, size_t size)
{
    zmq_msg_t msg;
    zmq_msg_init_size(&msg, size);
    zmq_msg_init_data(&msg, message, size, NULL, NULL);
    zmq_msg_send(&msg, socket, 0);
    zmq_msg_close(&msg);
}

void log_time_taken(struct timeval start, struct timeval end)
{
    double time_taken = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1e6;
    char log_filepath[256];
    snprintf(log_filepath, sizeof(log_filepath), "%s/log.txt", DIRECTORY);
    FILE *file = fopen(log_filepath, "a");
    if (file != NULL)
    {
        fprintf(file, "%f\n", time_taken);
        fclose(file);
    }
    else
    {
        perror("Failed to open log file");
    }
}

void write_data_to_file(FILE *file, char *data, size_t size)
{
    char *buffer = malloc(size);
    memcpy(buffer, data, size);
    fwrite(buffer, 1, size, file);
    free(buffer);
}

void *recv_data(void *arg)
{
    int thread_index = *(int *)arg;
    free(arg);
    void *receiver;
    connect_socket(&receiver);

    // Initialize
    int step = 0;
    bool is_port_complete = false;
    double bytes_received = 0.0;
    while (!is_port_complete)
    {
        // Timing
        struct timeval start, end;
        gettimeofday(&start, NULL);

        // Receive filename & construct filepath
        size_t filename_len;
        char *filename;
        recv_data_chunk(receiver, &filename, &filename_len);
        filename[filename_len] = '\0'; // Null-terminate the string

        char *filepath = construct_filepath(filename, step);
        // Opening file for appending
        FILE *file = fopen(filepath, "ab");

        // Logging
        printf("Step (%d), Receiving file: %s\n", step, filename);

        // Receive file chunks
        bool is_file_complete = false;
        while (!is_file_complete)
        {
            size_t chunk_size;
            char *data;
            recv_data_chunk(receiver, &data, &chunk_size);

            // Check if the file has been completely sent
            if (chunk_size == 0)
            {
                is_file_complete = true;
                continue;
            }

            write_data_to_file(file, data, chunk_size);
            // Logging Timing each 2 seconds
            bytes_received += chunk_size;
        }

        printf("Step (%d) Received file: %s\n", step, filename);

        // Close file
        fclose(file);
        free(filename);
        free(filepath);

        // Receive alert message
        char *alertMsg;
        size_t alert_size;
        recv_data_chunk(receiver, &alertMsg, &alert_size);
        int alert = atoi(alertMsg);

        // if 0 that means the port is complete and no more steps,
        // if 1 more files to come with the same step,
        // if 2 move to the next step by incrementing step

        // Logging And ack
        if (alert != 1)
        {
            printf("step (%d): received Augmentation files\n", step);
            // Ack message
            char ack_message[256];
            snprintf(ack_message, sizeof(ack_message), "step (%d): Received Augmentation files", step);
            send_data_chunk(receiver, ack_message, strlen(ack_message) + 1);

            // Timing
            gettimeofday(&end, NULL);
            log_time_taken(start, end);

            run_blob_detection_scripts(step);
        }

        // Process alert
        switch (alert)
        {
        case 1:
            continue;
        case 2:
            step++;
            break;
        default:
            is_port_complete = true;
            break;
        }
    }

    // Cleanup
    zmq_close(receiver);
    pthread_exit(NULL);
    return NULL;
}

int main()
{
    printf("Starting Receiver...\n");
    context = zmq_ctx_new();
    pthread_t full_data_thread;
    int *thread_index = malloc(sizeof(int));
    *thread_index = 0;
    pthread_create(&full_data_thread, NULL, recv_data, thread_index);
    pthread_join(full_data_thread, NULL);
    printf("All threads completed.\n");
    zmq_ctx_destroy(&context);
    return 0;
}