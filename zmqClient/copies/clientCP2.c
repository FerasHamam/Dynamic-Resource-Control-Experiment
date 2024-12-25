#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zmq.h>
#include <unistd.h>
#include <stdbool.h>
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>

void *context;

char *construct_filepath(const char *filename)
{
    char *filepath = (char *)malloc(256 * sizeof(char));
    char new_directory[256];
    char directory[128] = "/home/cc/zmqClient/data";

    if (strstr(filename, "reduced") != NULL)
    {
        snprintf(new_directory, sizeof(new_directory), "%s/%s/", directory, "reduced");
    }
    else if (strstr(filename, "delta") != NULL)
    {
        snprintf(new_directory, sizeof(new_directory), "%s/%s/", directory, "delta");
    }
    else
    {
        snprintf(new_directory, sizeof(new_directory), "%s/", directory);
    }

    sprintf(filepath, "%s%s", new_directory, filename);
    return filepath;
}

int recv_no_files()
{
    void *responder = zmq_socket(context, ZMQ_REP);
    if (zmq_bind(responder, "tcp://0.0.0.0:4444") != 0)
    {
        fprintf(stderr, "Failed to bind socket: %s\n", zmq_strerror(zmq_errno()));
        zmq_close(responder); // Close the socket if binding fails
        return -1;            // Return an error code
    }
    printf("Waiting to receive the number of files...\n");
    zmq_msg_t msg;
    zmq_msg_init(&msg);
    if (zmq_msg_recv(&msg, responder, 0) == -1)
    {
        fprintf(stderr, "Failed to receive message: %s\n", zmq_strerror(zmq_errno()));
        zmq_msg_close(&msg);
        zmq_msg_close(&msg);
        return -1;
    }
    int value;
    memcpy(&value, zmq_msg_data(&msg), sizeof(int));
    zmq_msg_init_size(&msg, strlen("ACK files!") + 1);
    memcpy(zmq_msg_data(&msg), "ACK files!", strlen("ACK files!") + 1);
    zmq_msg_send(&msg, responder, 0);
    zmq_close(responder);
    zmq_msg_close(&msg);
    return value;
}

const char *check_congestion_status(long bytes_received, long start_time, bool *is_congested)
{
    long current_time = time(NULL);
    double time_elapsed = difftime(current_time, start_time);

    double throughput = (double)bytes_received / time_elapsed;
    const double THRESHOLD = 5000.0; // threshold for congestion in bytes/sec

    if (throughput < THRESHOLD && !(*is_congested))
    {
        *is_congested = true;
        return "reduced";
    }
    else if (throughput >= THRESHOLD && *is_congested)
    {
        *is_congested = false;
        return "full";
    }
    return NULL;
}

void *recv_file(void *arg)
{
    int thread_index = *(int *)arg; // Retrieve thread index
    free(arg);
    void *responder = zmq_socket(context, ZMQ_REP);
    int port = 4445 + thread_index;
    char bind_address[50];
    snprintf(bind_address, sizeof(bind_address), "tcp://0.0.0.0:%d", port);
    if (zmq_bind(responder, bind_address) != 0)
    {
        fprintf(stderr, "Failed to bind socket: %s\n", zmq_strerror(zmq_errno()));
        return NULL;
    }
    printf("Binding to server on port %d\n", port);

    // step
    while (1)
    {
        // network variables
        zmq_msg_t msg;
        char ackMsg[256];
        int ackMsgSize = sizeof(ackMsg);

        // time variables
        struct timespec start, end;

        // congestion variables
        bool is_congested = false;
        long bytes_received = 0;
        long start_time = time(NULL);

        // step variables (Multiple data science jobs)
        int chunk_iteration = 0;
        bool is_file_complete = false;

        // waiting for a step to be initiated by the server
        zmq_msg_init(&msg);
        zmq_msg_recv(&msg, responder, 0);
        char *step = zmq_msg_data(&msg);
        if (atoi(step) == 0)
        {
            zmq_msg_close(&msg);
            break;
        }
        printf("Starting step: %s on port %i\n", step, port);
        zmq_msg_init_size(&msg, strlen("ACK step!") + 1);
        zmq_msg_send(&msg, responder, 0);

        // receive filename
        zmq_msg_init(&msg);
        zmq_msg_recv(&msg, responder, 0);
        char *data = zmq_msg_data(&msg);
        int length = zmq_msg_size(&msg);
        char *filename = (char *)malloc(length + 1);
        strcpy(filename, data);
        printf("Received filename: %s\n", filename);
        char *filepath = construct_filepath(filename);
        char ack[256];
        snprintf(ack, sizeof(ack), "Received Filename: %s", filename);
        zmq_msg_init_size(&msg, strlen(ack) + 1);
        memcpy(zmq_msg_data(&msg), ack, strlen(ack) + 1);
        zmq_msg_send(&msg, responder, 0);

        // opening file
        FILE *file = fopen(filepath, "ab");
        if (file == NULL)
        {
            fprintf(stderr, "Failed to open file %s\n", filename);
            free(filename);
            pthread_exit(NULL);
            return NULL;
        }

        // starting the clock
        clock_gettime(CLOCK_MONOTONIC, &start);

        while (!is_file_complete)
        {
            // receive the chunk
            zmq_msg_init(&msg);
            zmq_msg_recv(&msg, responder, 0);
            long chunk_size = zmq_msg_size(&msg);
            char *buffer = (char *)malloc(chunk_size);
            char *data = zmq_msg_data(&msg);
            if (chunk_size == 0 || buffer == NULL)
            {
                is_file_complete = true;
                continue;
            }
            memcpy(buffer, zmq_msg_data(&msg), chunk_size);
            fwrite(buffer, 1, chunk_size, file);
            free(buffer);

            // detect congestion
            bytes_received += chunk_size;
            const char *congestion_status = check_congestion_status(bytes_received, start_time, &is_congested);
            if (congestion_status)
            {
                snprintf(ackMsg, sizeof(ackMsg), "Chunk %d from file %s: Received Successfully! Status: %s",
                         chunk_iteration, filename, congestion_status);
            }
            else
            {
                snprintf(ackMsg, sizeof(ackMsg), "Chunk %d from file %s: Received Successfully!",
                         chunk_iteration, filename);
            }

            // send the ack
            zmq_msg_init_size(&msg, ackMsgSize + 1);
            // memcpy(zmq_msg_data(&msg), ackMsg, ackMsgSize + 1);
            zmq_msg_init_data(&msg, ackMsg, ackMsgSize, NULL, NULL);
            zmq_msg_send(&msg, responder, 0);
            chunk_iteration++;
        }

        // send the ack for close message with 0 bytes when the file is complete
        sprintf(ackMsg, "File %s received successfully!", filename);
        printf("%s\n", ackMsg);
        zmq_msg_init_size(&msg, ackMsgSize + 1);
        zmq_msg_init_data(&msg, ackMsg, ackMsgSize, NULL, NULL);
        zmq_msg_send(&msg, responder, 0);

        // Free resources
        fseek(file, 0, SEEK_SET);
        fclose(file);
        free(filename);
        free(filepath);
        zmq_msg_close(&msg);

        // end the clock
        clock_gettime(CLOCK_MONOTONIC, &end);
        double time_taken = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec);
        printf("It took %.6f seconds to receive file %s", time_taken, filename);
    }

    // Close the socket and exit the thread -> No more steps
    zmq_close(responder);
    pthread_exit(NULL);
    return NULL;
}

int main()
{
    printf("Starting client...\n");
    context = zmq_ctx_new();
    int no_files = recv_no_files();
    printf("Received number of files: %d\n", no_files);

    pthread_t threads[no_files];

    for (int i = 0; i < no_files; i++)
    {
        int *arg = malloc(sizeof(*arg));
        *arg = i;
        if (pthread_create(&threads[i], NULL, recv_file, arg) != 0)
        {
            perror("Error creating thread");
            return -1;
        }
    }

    for (int i = 0; i < no_files; i++)
    {
        pthread_join(threads[i], NULL);
    }

    printf("Client sockets shutting down.\n");
    zmq_ctx_destroy(&context);
    return 0;
}