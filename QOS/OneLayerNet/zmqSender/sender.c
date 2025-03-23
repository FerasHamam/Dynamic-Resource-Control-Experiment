#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zmq.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <sys/time.h>


#define BASE_PORT 5555
#define SHARED_IP "10.10.10.4"
#define DETICATED_IP "10.10.10.8"
#define NUM_STEPS 60
#define DIRECTORY "../data/"
#define CHUNK_SIZE 16 * 1024 * 1024

void *context;
typedef struct
{
    char **filenames;
    int num_files;
    int thread_index;
} ThreadArgs;

void sleep_ms(double milliseconds)
{
    struct timespec ts;
    ts.tv_sec = (long)(milliseconds / 1000.0);
    ts.tv_nsec = (long)((milliseconds - ((long)(milliseconds / 1000.0) * 1000.0)) * 1000000.0);
    nanosleep(&ts, NULL);
}

void connect_socket(void **socket, int thread_index)
{
    char bind_address[50];
    if (thread_index == 0)
        snprintf(bind_address, sizeof(bind_address), "tcp://%s:%d", DETICATED_IP, BASE_PORT);
    else
        snprintf(bind_address, sizeof(bind_address), "tcp://%s:%d", SHARED_IP, BASE_PORT);
    *socket = zmq_socket(context, ZMQ_PAIR);
    int socket_fd = (uintptr_t)*socket;
    zmq_connect(*socket, bind_address);
    printf("Connected to port %d\n", BASE_PORT);
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

int open_file(FILE **file, const char *filename)
{
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s%s", DIRECTORY, filename);
    *file = fopen(filepath, "rb");
    if (*file == NULL)
    {
        perror("Error opening file");
        return 1;
    }
    return 0;
}

void *send_data(void *arg)
{
    // Read args
    ThreadArgs *args = (ThreadArgs *)arg;
    char **filenames = args->filenames;
    int num_files = args->num_files;
    int thread_index = args->thread_index;

    void *sender;
    connect_socket(&sender, thread_index);
    int step = 0;
    struct timeval start, end;
    while (step < NUM_STEPS)
    {
        // Send file
        for (int i = 0; i < num_files; i++)
        {
            // Send file name
            send_data_chunk(sender, filenames[i], strlen(filenames[i]) + 1);
            gettimeofday(&start, NULL);
            // Open file for reading
            FILE *file;
            if (open_file(&file, filenames[i]))
            {
                zmq_close(sender);
                return NULL;
            };

            // Send file data of 1mb chunks
            size_t bytes_read;
            size_t chunk_size = CHUNK_SIZE;
            char *buffer = (char *)malloc(chunk_size);
            while ((bytes_read = fread(buffer, 1, chunk_size, file)) > 0)
            {
                if (bytes_read <= 0)
                {
                    printf("Failed to read from file %s\n", filenames[i]);
                    break;
                }
                send_data_chunk(sender, buffer, bytes_read);
            }

            // Send the close message with 0 bytes
            send_data_chunk(sender, "", 0);

            // Alert message
            // if 0 that means the port is complete and no more steps,
            // if 1 more files to come with the same step,
            // if 2 move to the next step by incrementing step
            char *message = (step == NUM_STEPS - 1 && i == num_files - 1) ? "0" : (i < num_files - 1) ? "1"
                                                                                                      : "2";
            send_data_chunk(sender, message, strlen(message) + 1);

            // Ack message
            if (*message == '1')
            {
                continue;
            }

            size_t size;
            char *ack_message;
            recv_data_chunk(sender, &ack_message, &size);
            printf("Received ack message: %s\n", ack_message);
        }
        // Increment step
        gettimeofday(&end, NULL);
        double seconds_diff = (double)(end.tv_sec - start.tv_sec);
        double microseconds_diff = (double)(end.tv_usec - start.tv_usec) / 1000000.0;

        double elapsed = seconds_diff + microseconds_diff;
        double remaining = 60.0 - elapsed;
        if(remaining < 0)
            remaining = 0;
        printf("Step %d took %f seconds, sleeping for %f seconds\n", step, elapsed, remaining);
        sleep_ms(remaining * 1000);
        printf("\n--- Step %d completed ---\n", step);
        step++;
    }

    zmq_close(sender);
    pthread_exit(NULL);
    return NULL;
}

int main()
{
    printf("Starting Sender...\n");
    context = zmq_ctx_new();
    pthread_t thread1;
    // Create thread arguments 1
    ThreadArgs args1;
    char *filenames1[] = {"full_data_xgc.bin"};
    args1.filenames = filenames1;
    args1.num_files = 1;
    args1.thread_index = 1;
    // Create a thread for sending part1 of full data
    if (pthread_create(&thread1, NULL, send_data, &args1) != 0)
    {
        fprintf(stderr, "Error: Failed to create send file thread\n");
        return EXIT_FAILURE;
    }

    // Wait for threads to finish
    pthread_join(thread1, NULL);

    // Clean up ZeroMQ context
    zmq_ctx_destroy(context);
    return EXIT_SUCCESS;
}