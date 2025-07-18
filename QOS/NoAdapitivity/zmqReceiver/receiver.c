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

#define BASE_PORT 5555
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

void connect_socket(void **socket, int thread_index)
{
    char bind_address[50];
    snprintf(bind_address, sizeof(bind_address), "tcp://0.0.0.0:%d", BASE_PORT + thread_index);
    *socket = zmq_socket(context, ZMQ_PAIR);
    zmq_bind(*socket, bind_address);
    printf("Binding to port %d\n", BASE_PORT + thread_index);
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

void log_time_taken(struct timeval start, struct timeval end, int thread_index)
{
    double time_taken = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1e6;
    char log_filepath[256];
    snprintf(log_filepath, sizeof(log_filepath), "%s/log%d.txt", DIRECTORY, thread_index);
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
    connect_socket(&receiver, thread_index);

    int step = 0;
    bool is_port_complete = false;
    while (!is_port_complete)
    {
        struct timeval start, end;
        
        // Receive filename
        char *filename;
        size_t filename_len;
        recv_data_chunk(receiver, &filename, &filename_len);
        if (filename_len > 0)
        {
            filename[filename_len - 1] = '\0';
            printf("Received filename: %s\n", filename);
            char *filepath = construct_filepath(filename, step);
            
            if (filepath)
            {
                FILE *file = fopen(filepath, "ab");
                gettimeofday(&start, NULL);
                if (file)
                {
                    // Receive file chunks
                    bool is_file_complete = false;
                    while (!is_file_complete)
                    {
                        char *data;
                        size_t chunk_size;
                        recv_data_chunk(receiver, &data, &chunk_size);

                        if (chunk_size == 0)
                        {
                            is_file_complete = true;
                            gettimeofday(&end, NULL);
                            free(data);
                        }
                        else
                        {
                            write_data_to_file(file, data, chunk_size);
                            free(data);
                        }
                    }
                    fclose(file);
                }
                free(filepath);
            }
            free(filename);
        }
        
        // Process alert
        char *alertMsg;
        size_t alert_size;
        recv_data_chunk(receiver, &alertMsg, &alert_size);
        int alert = atoi(alertMsg);
        free(alertMsg);

        if (alert != 1)
        {
            log_time_taken(start, end, thread_index);
            char ack_message[256];
            snprintf(ack_message, sizeof(ack_message), "step (%d): Received files", step);
            send_data_chunk(receiver, ack_message, strlen(ack_message) + 1);
        }

        switch (alert)
        {
        case 1:
            continue;
        case 2:
            step++;
            break;
        default:
            is_port_complete = true;
        }
    }

    zmq_close(receiver);
    pthread_exit(NULL);
    return NULL;
}

int main()
{
    printf("Starting Receiver...\n");
    context = zmq_ctx_new();
    pthread_t partial_data1;
    int *thread_index1 = malloc(sizeof(int));
    *thread_index1 = 0;
    pthread_create(&partial_data1, NULL, recv_data, thread_index1);
    pthread_join(partial_data1, NULL);
    printf("All threads completed.\n");
    zmq_ctx_destroy(&context);
    return 0;
}