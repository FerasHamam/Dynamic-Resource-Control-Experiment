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
int index_red = 0, index_aug = 0;
double time_taken_red[10000] = {0}, time_taken_aug[10000] = {0};

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

void log_time_taken(struct timeval start, struct timeval end, int thread_index, bool next_step)
{
    double time_taken = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1e6;
    if (thread_index == 0)
    {
        time_taken_red[index_red] += time_taken;
        if (next_step)
        {
            index_red++;
        }
    }
    else
    {
        time_taken_aug[index_aug] += time_taken;
        if (next_step)
        {
            FILE *log_file = fopen("../data/log.txt", "a");
            double time_taken_per_step = time_taken_aug[index_aug];
            if (time_taken_red[index_aug] > time_taken_aug[index_aug])
                time_taken_per_step = time_taken_red[index_aug];
            fprintf(log_file, "%f\n", time_taken_per_step);
            fclose(log_file);
            index_aug++;
        }
    }
}

void write_data_to_file(FILE *file, char *data, size_t size)
{
    // fork process
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
    struct timeval start, end, chunk_time_start, chunk_time_end;
    while (!is_port_complete)
    {
        // Receive filename
        char *filename;
        size_t filename_len;
        recv_data_chunk(receiver, &filename, &filename_len);
        if (filename_len > 0)
        {
            gettimeofday(&start, NULL);
            filename[filename_len - 1] = '\0';
            printf("Received filename: %s\n", filename);
            char *filepath = construct_filepath(filename, step);

            if (filepath)
            {
                FILE *file = fopen(filepath, "ab");
                if (file)
                {
                    // Receive file chunks
                    bool is_file_complete = false;
                    char *data;
                    size_t chunk_size;
                    
                    // monitor data chunk time
                    gettimeofday(&chunk_time_start, NULL);
                    recv_data_chunk(receiver, &data, &chunk_size);
                    gettimeofday(&chunk_time_end, NULL);
                    double chunk_time_taken = (chunk_time_end.tv_sec - chunk_time_start.tv_sec) + (chunk_time_end.tv_usec - chunk_time_start.tv_usec) / 1e6;
                    printf("step (%d): Received chunk of size %ld, time taken: %f\n", step, chunk_size, chunk_time_taken);

                    // send time taken
                    char time_str[32];
                    snprintf(time_str, sizeof(time_str), "%f", chunk_time_taken);
                    send_data_chunk(receiver, time_str, strlen(time_str) + 1);
                    write_data_to_file(file, data, chunk_size);
                    // Free the received data
                    free(data);
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
        run_blob_detection_scripts(step);

        if (alert != 1)
        {
            char ack_message[256];
            snprintf(ack_message, sizeof(ack_message), "step (%d): Received %s", step, thread_index == 0 ? "Reduced data" : "Aug data");
            send_data_chunk(receiver, ack_message, strlen(ack_message) + 1);
        }

        switch (alert)
        {
        case 1:
            gettimeofday(&end, NULL);
            log_time_taken(start, end, thread_index, false);
            continue;
        case 2:
            gettimeofday(&end, NULL);
            log_time_taken(start, end, thread_index, true);
            while (index_aug != index_red)
            {
                continue;
                usleep(1000);
            }
            step++;
            break;
        default:
            gettimeofday(&end, NULL);
            log_time_taken(start, end, thread_index, true);
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
    pthread_t partial_data1, partial_data2;
    int *thread_index1 = malloc(sizeof(int));
    *thread_index1 = 0;
    pthread_create(&partial_data1, NULL, recv_data, thread_index1);

    int *thread_index2 = malloc(sizeof(int));
    *thread_index2 = 1;
    pthread_create(&partial_data2, NULL, recv_data, thread_index2);

    pthread_join(partial_data1, NULL);
    pthread_join(partial_data2, NULL);

    FILE *log_file = fopen("../data/log_final.txt", "a");
    for (int i = 0; i < index_aug; i++)
    {
        double time_taken_per_step = time_taken_aug[i];
        if (time_taken_red[i] > time_taken_aug[i])
            time_taken_per_step = time_taken_red[i];
        fprintf(log_file, "%f\n", time_taken_per_step);
    }
    fclose(log_file);

    printf("All threads completed.\n");
    zmq_ctx_destroy(&context);
    return 0;
}