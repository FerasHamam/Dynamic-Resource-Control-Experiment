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

#define BASE_PORT 4444
#define CLIENT_IP "CLIENT_IP"
#define NUM_STEPS 1
#define DIRECTORY "../data/"

void *context;
typedef struct
{
    char **filenames;
    int num_files;
    int thread_index;
} ThreadArgs;

void connect_socket(void **socket)
{
    char bind_address[50];
    snprintf(bind_address, sizeof(bind_address), "tcp://%s:%d", CLIENT_IP, BASE_PORT);
    *socket = zmq_socket(context, ZMQ_PAIR);
    zmq_connect(*socket, bind_address);
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
    connect_socket(&sender);
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
            size_t chunk_size = 1024 * 1024;
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
    pthread_t thread;
    // Filenames to be sent
    char *filenames[] = {"full_data_xgc.bin"};
    // Create thread arguments
    ThreadArgs args;
    args.filenames = filenames;
    args.num_files = 1;
    args.thread_index = 0;
    // Create a thread for sending files
    if (pthread_create(&thread, NULL, send_data, &args) != 0)
    {
        fprintf(stderr, "Error: Failed to create send file thread\n");
        return EXIT_FAILURE;
    }
    pthread_join(thread, NULL);
    // Clean up ZeroMQ context
    zmq_ctx_destroy(context);
    return EXIT_SUCCESS;
}