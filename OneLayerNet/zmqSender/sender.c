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
#include <time.h>

#define BASE_PORT 5555
#define SHARED_IP "IP"
#define DETICATED_IP "IP"
#define NUM_STEPS 2
#define DIRECTORY "../data/"
#define CHUNK_SIZE 1024 * 1024

void *context;
typedef struct
{
    char **filenames;
    int num_files;
    int thread_index;
} ThreadArgs;

void connect_socket(void **socket, int thread_index)
{
    char bind_address[50];
    if (thread_index == 0)
        snprintf(bind_address, sizeof(bind_address), "tcp://%s:%d", DETICATED_IP, BASE_PORT + thread_index);
    else
        snprintf(bind_address, sizeof(bind_address), "tcp://%s:%d", SHARED_IP, BASE_PORT + thread_index);
    *socket = zmq_socket(context, ZMQ_PAIR);
    int socket_fd = (uintptr_t)*socket; // Cast void* to int
    if (thread_index == 0)
    {
        setsockopt(socket_fd, SOL_SOCKET, SO_BINDTODEVICE, "enp8s0", strlen("enp8s0"));
    }
    else
    {
        setsockopt(socket_fd, SOL_SOCKET, SO_BINDTODEVICE, "enp7s0", strlen("enp7s0"));
    }
    zmq_connect(*socket, bind_address);
    printf("Connected to port %d\n", BASE_PORT + thread_index);
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
    while (step < NUM_STEPS)
    {
        // Send file
        for (int i = 0; i < num_files; i++)
        {
            // Send file name
            send_data_chunk(sender, filenames[i], strlen(filenames[i]) + 1);

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
    pthread_t thread1, thread2;
    // Filenames to be sent
    // Create thread arguments 1
    ThreadArgs args1;
    char *filenames1[] = {"small_portion.bin"};
    args1.filenames = filenames1;
    args1.num_files = 1;
    args1.thread_index = 0;
    // Create a thread for sending part1 of full data
    if (pthread_create(&thread1, NULL, send_data, &args1) != 0)
    {
        fprintf(stderr, "Error: Failed to create send file thread\n");
        return EXIT_FAILURE;
    }

    // Create thread arguments 2
    ThreadArgs args2;
    char *filenames2[] = {"large_portion.bin"};
    args2.filenames = filenames2;
    args2.num_files = 3;
    args2.thread_index = 1;

    // Create a thread for sending part2 of full data
    if (pthread_create(&thread2, NULL, send_data, &args2) != 0)
    {
        fprintf(stderr, "Error: Failed to create send file thread\n");
        return EXIT_FAILURE;
    }

    // Wait for threads to finish
    pthread_join(thread1, NULL);
    pthread_join(thread2, NULL);

    // Clean up ZeroMQ context
    zmq_ctx_destroy(context);
    return EXIT_SUCCESS;
}