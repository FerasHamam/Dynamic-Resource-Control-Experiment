#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zmq.h>
#include <pthread.h>
#include <unistd.h>
#include <dirent.h>

#define CLIENT_IP "CLIENT_IP" // Update with the client's IP
#define BASE_PORT 5555

void *context;

typedef struct
{
    char *filename;
    int thread_index;
} ThreadArgs;

void *send_file(void *arg)
{
    ThreadArgs *args = (ThreadArgs *)arg;
    char *filename = args->filename;
    int thread_index = args->thread_index;

    // Initialize the ZMQ socket for file transfer
    zmq_msg_t msg; 
    void *sender = zmq_socket(context, ZMQ_PUSH);
    char bind_address[50];
    int port = BASE_PORT + thread_index + 1;
    snprintf(bind_address, sizeof(bind_address), "tcp://%s:%d", CLIENT_IP, port);

    if (zmq_connect(sender, bind_address) != 0)
    {
        fprintf(stderr, "Failed to connect to client: %s\n", zmq_strerror(zmq_errno()));
        free(args);
        return NULL;
    }

    // Send the filename
    char filename_with_null[strlen(filename) + 1];
    strcpy(filename_with_null, filename);
    zmq_msg_init_data(&msg, filename_with_null, strlen(filename_with_null) + 1, NULL, NULL);
    zmq_msg_send(&msg, sender, 0);

    // Construct the file path
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "../data/%s", filename);

    // Open the file
    FILE *file = fopen(filepath, "rb");
    if (file == NULL)
    {
        fprintf(stderr, "Failed to open file: %s\n", filepath);
        zmq_close(sender);
        free(args);
        return NULL;
    }

    // Send file data in chunks
    size_t chunk_size = 1024 * 1024;
    char *buffer = malloc(chunk_size);
    if (!buffer)
    {
        fprintf(stderr, "Failed to allocate memory\n");
        fclose(file);
        zmq_close(sender);
        free(args);
        return NULL;
    }

    // Repeatedly send the file
    int iteration = 0;
    while (1)
    {   
        printf("Thread %d: Sending file %s, iteration %d\n", thread_index, filename, iteration);
        // Reset file pointer to the beginning
        rewind(file);

        // Send file data in chunks
        size_t bytes_read;
        while ((bytes_read = fread(buffer + 50, 1, chunk_size, file)) > 0)
        {
            // Modify the chunk with the thread index and iteration
            snprintf(buffer, 50, "Index:%d Iteration:%d\n", thread_index, iteration);
            zmq_msg_init_data(&msg, buffer, bytes_read + 50, NULL, NULL);
            zmq_msg_send(&msg, sender, 0);
        }

        // Send an empty message to signal end of file
        zmq_msg_init_data(&msg, "", 0, NULL, NULL);
        zmq_msg_send(&msg, sender, 0);

        // Wait before sending the file again
        sleep(10*(thread_index+2));
        iteration++;
    }

    free(buffer);
    fclose(file);
    zmq_close(sender);
    free(args);

    return NULL;
}

char **get_filenames(const char *directory, int *file_count)
{
    DIR *dir;
    struct dirent *entry;
    char **filenames = malloc(256 * sizeof(char *));
    *file_count = 0;

    if ((dir = opendir(directory)) != NULL)
    {
        while ((entry = readdir(dir)) != NULL)
        {
            if (entry->d_type == DT_REG)
            {
                filenames[*file_count] = strdup(entry->d_name);
                (*file_count)++;
            }
        }
        closedir(dir);
    }
    else
    {
        perror("opendir");
        return NULL;
    }

    return filenames;
}

int main()
{
    printf("Starting File Sender...\n");

    // Initialize ZMQ context
    context = zmq_ctx_new();

    // Step 1: Communicate the total number of files to the client
    void *init_socket = zmq_socket(context, ZMQ_PUSH);
    char init_address[50];
    snprintf(init_address, sizeof(init_address), "tcp://%s:%d", CLIENT_IP, BASE_PORT);
    printf("Connecting to: %s\n", init_address);
    if (zmq_connect(init_socket, init_address) != 0)
    {
        fprintf(stderr, "Failed to bind to BASE_PORT: %s\n", zmq_strerror(zmq_errno()));
        zmq_ctx_term(context);
        return EXIT_FAILURE;
    }

    // Get filenames from the "data" directory
    int file_count;
    char **filenames = get_filenames("../data", &file_count);
    if (!filenames)
    {
        zmq_close(init_socket);
        zmq_ctx_term(context);
        return EXIT_FAILURE;
    }

    // Send the number of files to the client
    zmq_send(init_socket, &file_count, sizeof(file_count), 0);
    zmq_close(init_socket);

    // Step 2: Create a thread for each file to send it
    pthread_t threads[file_count];

    for (int i = 0; i < file_count; i++)
    {
        ThreadArgs *args = malloc(sizeof(ThreadArgs));
        args->filename = filenames[i];
        args->thread_index = i;

        if (pthread_create(&threads[i], NULL, send_file, args) != 0)
        {
            fprintf(stderr, "Failed to create thread for file: %s\n", filenames[i]);
            free(args);
        }
    }

    // Wait for all threads to complete
    for (int i = 0; i < file_count; i++)
    {
        pthread_join(threads[i], NULL);
    }

    // Cleanup
    for (int i = 0; i < file_count; i++)
    {
        free(filenames[i]);
    }
    free(filenames);

    zmq_ctx_destroy(context);
    return 0;
}