#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zmq.h>
#include <unistd.h>
#include <stdbool.h>
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>

typedef enum
{
    FULL,
    REDUCED
} DataQuality;

void *context;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER; // Mutex to synchronize threads
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;    // Condition variable to notify threads
int threads_done = 0;                              // Counter to track when both threads are done
int step = 0;                                      // Current step to keep track of the step number

// Register congestion status at the start of the step
DataQuality data_quality_for_step = FULL; // Status for current step

// Performance measurement variables
struct timespec start, end;
DataQuality shared_data_quality = FULL;
atomic_long total_bytes_received = 0;

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

// Congestion monitor thread that updates the data quality
void *congestion_monitor_thread(void *arg)
{
    long last_update_time = time(NULL);
    const long congestion_check_interval = 5; // Check every 5 seconds

    while (1)
    {
        // Termination mechanism
        if (context == NULL)
        {
            break;
        }

        long current_time = time(NULL);
        if (current_time - last_update_time >= congestion_check_interval)
        {
            long bytes_received = atomic_load(&total_bytes_received);
            double throughput = (double)bytes_received / congestion_check_interval;
            const double THRESHOLD = 5000.0; // threshold for congestion in bytes/sec

            pthread_mutex_lock(&mutex); // Lock before updating shared data quality
            if (throughput < THRESHOLD)
            {
                if (shared_data_quality == FULL)
                {
                    shared_data_quality = REDUCED;
                    printf("Network congestion detected: Throughput %.2f bytes/sec (Reduced mode)\n", throughput);
                }
            }
            else
            {
                if (shared_data_quality == REDUCED)
                {
                    shared_data_quality = FULL;
                    printf("Network throughput: %.2f bytes/sec (Full mode)\n", throughput);
                }
            }
            pthread_mutex_unlock(&mutex); // Unlock after updating shared data quality

            // Reset total bytes received and update the last check time
            atomic_store(&total_bytes_received, 0);
            last_update_time = current_time;
        }
    }
    return NULL;
}

void run_blob_detection_scripts(DataQuality data_quality, int step)
{
    if (data_quality == REDUCED)
    {
        printf("Running Reduced blob detection...\n");
        system("./venv/bin/python ~/zmq/scripts/data_to_blob_detection.py --app_name xgc --data_type reduced --path ~/zmq/data/reduced/ --output_name xgc_reduced.png");
    }
    else
    {
        printf("Running Full blob detection...\n");
        system("./venv/bin/python ~/zmq/scripts/combine.py");
        system("./venv/bin/python ~/zmq/scripts/data_to_blob_detection.py --app_name xgc --data_type full --path ./ --output_name xgc_full.png");
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    double time_taken = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec);
    printf("It took %.6f seconds to run the blob detection scripts for step %d\n", time_taken, step);
}

// Function to handle receiving files for high and low quality data
void *recv_data(void *arg)
{
    // Read args
    int port = *(int *)arg;
    free(arg);

    // Initialize the socket
    char bind_address[50];
    void *socket = zmq_socket(context, ZMQ_REP);
    snprintf(bind_address, sizeof(bind_address), "tcp://0.0.0.0:%d", port);
    zmq_bind(socket, bind_address);
    printf("Binding to port %d\n", port);

    // Network variables
    zmq_msg_t msg;
    char ackMsg[256];
    int ackMsgSize = sizeof(ackMsg);
    bool is_port_complete = false;

    // Start the clock
    clock_gettime(CLOCK_MONOTONIC, &start);

    while (!is_port_complete)
    {
        // Receive filename
        zmq_msg_init(&msg);
        zmq_msg_recv(&msg, socket, 0); // Receive filename
        char *filename = zmq_msg_data(&msg);
        printf("Step %i, Received filename: %s\n", step, filename);
        char *filepath = construct_filepath(filename);
        zmq_msg_close(&msg);

        // Send ACK for file name
        snprintf(ackMsg, sizeof(ackMsg), "Received Filename: %s", filename);
        zmq_msg_init_size(&msg, strlen(ackMsg) + 1);
        memcpy(zmq_msg_data(&msg), ackMsg, strlen(ackMsg) + 1);
        zmq_msg_send(&msg, socket, 0);

        // Opening file for appending data
        FILE *file = fopen(filepath, "ab");
        if (file == NULL)
        {
            fprintf(stderr, "Failed to open file %s\n", filename);
            zmq_msg_close(&msg);
            free(filename);
            return NULL;
        }

        // Receiving a single file at a time
        bool is_file_complete = false;
        int chunk_iteration = 0;
        while (!is_file_complete)
        {
            zmq_msg_init(&msg);
            zmq_msg_recv(&msg, socket, 0); // Receive the data chunk (d)

            if (zmq_msg_size(&msg) == 0)
            {
                is_file_complete = true;
                continue;
            }

            // Process the data chunk
            long chunk_size = zmq_msg_size(&msg);
            char *buffer = (char *)malloc(chunk_size);
            memcpy(buffer, zmq_msg_data(&msg), chunk_size);
            fwrite(buffer, 1, chunk_size, file);
            free(buffer);
            atomic_store(&total_bytes_received, atomic_load(&total_bytes_received) + chunk_size);

            // Acknowledge chunk receipt
            snprintf(ackMsg, sizeof(ackMsg), "Chunk Received Successfully! Status: %s",
                     data_quality_for_step == FULL ? "FULL" : "REDUCED");
            zmq_msg_init_size(&msg, ackMsgSize + 1);
            zmq_msg_init_data(&msg, ackMsg, ackMsgSize, NULL, NULL);
            zmq_msg_send(&msg, socket, 0);

            // Increment chunk iteration
            chunk_iteration++;
        }

        // Send final acknowledgment that the file is complete
        sprintf(ackMsg, "File %s received successfully!", filename);
        printf("%s\n", ackMsg);
        zmq_msg_init_size(&msg, ackMsgSize + 1);
        zmq_msg_init_data(&msg, ackMsg, ackMsgSize, NULL, NULL);
        zmq_msg_send(&msg, socket, 0);

        // Free resources
        fclose(file);
        free(filename);
        free(filepath);

        // Check if the port is complete
        zmq_msg_init(&msg);
        zmq_msg_recv(&msg, socket, 0);

        // if 0 that means the port is complete and no more steps,
        // if 1 more files to come with the same step,
        // if 2 move to the next step by incrementing step
        int alert = atoi(zmq_msg_data(&msg));
        if (alert == 0)
        {
            is_port_complete = true;
            break;
        }
        if (alert == 1)
            continue;
        if (alert == 2)
        {
            // Only allow the high quality thread to run blob detection after both threads finish
            pthread_mutex_lock(&mutex);
            threads_done++;

            // Wait until both threads have completed and port is 4445
            while (threads_done < 2)
            {
                pthread_cond_wait(&cond, &mutex); // Wait for the condition variable to be signaled
            }

            // Only run the blob detection scripts if both threads are done and port is 4445
            if (threads_done == 2 && port == 4445)
            {
                run_blob_detection_scripts(data_quality_for_step, step);

                // End the clock for performance measurement
                clock_gettime(CLOCK_MONOTONIC, &end);
                double time_taken = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec);
                printf("It took %.6f seconds to receive file %s\n", time_taken, filename);

                // Update the data quality for the next step
                data_quality_for_step = shared_data_quality;
                step++;

                // Start a new clock for the next step
                clock_gettime(CLOCK_MONOTONIC, &start);
            }

            pthread_mutex_unlock(&mutex);
        }
    }

    zmq_close(socket);
    return NULL;
}

int main()
{
    printf("Starting client...\n");
    context = zmq_ctx_new();

    pthread_t high_quality_thread, low_quality_thread, congestion_thread;

    pthread_create(&congestion_thread, NULL, congestion_monitor_thread, NULL);

    int *port_high = malloc(sizeof(int));
    *port_high = 4445; // High quality port
    pthread_create(&high_quality_thread, NULL, recv_data, port_high);

    int *port_low = malloc(sizeof(int));
    *port_low = 4446; // Low quality port
    pthread_create(&low_quality_thread, NULL, recv_data, port_low);

    pthread_join(high_quality_thread, NULL);
    pthread_join(low_quality_thread, NULL);
    printf("Client sockets shut down.\n");
    zmq_ctx_destroy(&context);
    pthread_join(congestion_thread, NULL);
    printf("Congestion monitoring shut down.\n");

    return 0;
}
