#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zmq.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/stat.h>

#define SERVER_IP "0.0.0.0" // Update with the sender's IP
#define BASE_PORT 5555
#define OUTPUT_DIR "../data"

void *context;

typedef struct
{
    int thread_index;
    int total_files;
} ThreadArgs;

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

// Thread function to receive a file
void *receive_file(void *arg)
{
    ThreadArgs *args = (ThreadArgs *)arg;
    int thread_index = args->thread_index;

    // Initialize ZMQ socket
    void *receiver = zmq_socket(context, ZMQ_PULL);
    char bind_address[50];
    int port = BASE_PORT + thread_index + 1;
    snprintf(bind_address, sizeof(bind_address), "tcp://%s:%d", SERVER_IP, port);

    if (zmq_bind(receiver, bind_address) != 0)
    {
        fprintf(stderr, "Failed to bind to port %d: %s\n", port, zmq_strerror(zmq_errno()));
        free(args);
        return NULL;
    }

    // Receive the filename first
    zmq_msg_t msg;
    zmq_msg_init(&msg);
    if (zmq_msg_recv(&msg, receiver, 0) == -1)
    {
        fprintf(stderr, "Failed to receive filename: %s\n", zmq_strerror(zmq_errno()));
        zmq_msg_close(&msg);
        zmq_close(receiver);
        free(args);
        return NULL;
    }
    char filename[128];
    snprintf(filename, sizeof(filename), "%s", (char *)zmq_msg_data(&msg));
    zmq_msg_close(&msg);

    int iteration = 0;

    // Iteration to receive the file data
    while (1)
    {
        char directory[128];
        char filepath[256];
        snprintf(directory, sizeof(directory), "%s/%d", OUTPUT_DIR, iteration);
        snprintf(filepath, sizeof(filepath), "%s/%s", directory, filename);
        create_directories(directory);
        // Open output file
        FILE *output_file = fopen(filepath, "wb");
        if (!output_file)
        {
            fprintf(stderr, "Failed to create output file: %s\n", filename);
            zmq_close(receiver);
            free(args);
            return NULL;
        }

        // Receive file data
        zmq_msg_t message;
        while (1)
        {
            zmq_msg_init(&message);
            zmq_msg_recv(&message, receiver, 0);

            size_t message_size = zmq_msg_size(&message);
            if (message_size == 0)
            { // End of file signal
                printf("Iteration %d, Thread %d: File %s received completely.\n", iteration, thread_index, filename);
                break;
            }

            long chunk_size = zmq_msg_size(&message);
            char *buffer = malloc(chunk_size);
            memcpy(buffer, zmq_msg_data(&message), chunk_size);
            fwrite(buffer, 1, chunk_size, output_file);
            free(buffer);
        }
        fclose(output_file);
        zmq_msg_close(&message);
        iteration++;
    }

    zmq_close(receiver);
    free(args);
    return NULL;
}

int main()
{
    printf("Starting File Receiver...\n");

    // Initialize ZMQ context
    context = zmq_ctx_new();

    // Step 1: Receive the total number of files from the sender
    void *init_socket = zmq_socket(context, ZMQ_PULL);
    char init_address[50];
    snprintf(init_address, sizeof(init_address), "tcp://%s:%d", SERVER_IP, BASE_PORT);
    printf("Binding to: %s\n", init_address);
    if (zmq_bind(init_socket, init_address) != 0)
    {
        fprintf(stderr, "Failed to connect to BASE_PORT: %s\n", zmq_strerror(zmq_errno()));
        zmq_ctx_term(context);
        return EXIT_FAILURE;
    }

    int total_files;
    zmq_recv(init_socket, &total_files, sizeof(total_files), 0);
    printf("Total files to receive: %d\n", total_files);
    zmq_close(init_socket);

    // Step 2: Create output directory
    mkdir(OUTPUT_DIR, 0777);

    // Step 3: Create a thread for each file to receive it
    pthread_t threads[total_files];

    for (int i = 0; i < total_files; i++)
    {
        ThreadArgs *args = malloc(sizeof(ThreadArgs));
        args->thread_index = i;
        args->total_files = total_files;

        if (pthread_create(&threads[i], NULL, receive_file, args) != 0)
        {
            fprintf(stderr, "Failed to create thread for file %d\n", i);
            free(args);
        }
    }

    // Wait for all threads to complete
    for (int i = 0; i < total_files; i++)
    {
        pthread_join(threads[i], NULL);
    }

    zmq_ctx_destroy(context);
    printf("File receiving complete.\n");

    return 0;
}