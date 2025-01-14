#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zmq.h>
#include <pthread.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/time.h>

#define CLIENT_IP "CLIENT_IP" // Update with the client's IP
#define BASE_PORT 5555

void *context;

typedef struct
{
    char *filename;
    int thread_index;
    int interval;
} ThreadArgs;

void *send_file(void *arg)
{
    ThreadArgs *args = (ThreadArgs *)arg;
    char *filename = args->filename;
    int thread_index = args->thread_index;
    int interval = args->interval;

    printf("Thread %d: Sending file %s using interval %d\n", thread_index, filename, interval);
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
        // Timing
        struct timeval start, end;
        gettimeofday(&start, NULL);
        printf("Thread %d: Sending file %s, iteration %d\n", thread_index, filename, iteration);
        // Reset file pointer to the beginning
        rewind(file);

        // Send file data in chunks
        size_t bytes_read;
        while ((bytes_read = fread(buffer, 1, chunk_size, file)) > 0)
        {
            // Modify the chunk with the thread index and iteration
            zmq_msg_init_data(&msg, buffer, bytes_read, NULL, NULL);
            zmq_msg_send(&msg, sender, 0);
        }

        // Send an empty message to signal end of file
        zmq_msg_init_data(&msg, "", 0, NULL, NULL);
        zmq_msg_send(&msg, sender, 0);

        // Wait before sending the file again
        double elapsed;
        do{
            usleep(100000);
            gettimeofday(&end, NULL);
            elapsed = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;
        }while(elapsed < interval);

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

int *get_intervals(const char *filename, int *count) {
    FILE *file;
    int *intervals = NULL;
    int capacity = 10;

    intervals = (int *)malloc(capacity * sizeof(int));
    if (intervals == NULL) {
        perror("Memory allocation failed");
        return NULL;
    }

    file = fopen(filename, "r");
    if (file == NULL) {
        perror("Error opening file");
        free(intervals);
        return NULL;
    }

    // Read intervals from the file into the dynamic array
    int temp;
    *count = 0;
    while (fscanf(file, "%d", &temp) == 1) {
        if (*count >= capacity) {
            // Increase the array size
            capacity *= 2;
            int *new_intervals = (int *)realloc(intervals, capacity * sizeof(int));
            if (new_intervals == NULL) {
                perror("Memory reallocation failed");
                free(intervals);
                fclose(file);
                return NULL;
            }
            intervals = new_intervals;
        }
        intervals[(*count)++] = temp;
    }

    fclose(file);
    return intervals;  // Return the dynamic array
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
        zmq_ctx_destroy(context);
        return EXIT_FAILURE;
    }

    // Get intervals from the "intervals.txt" file
    char *intervals_filename = "../intervals.txt";
    int *intervals = get_intervals(intervals_filename, &file_count);

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
        args->interval = intervals[i];

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