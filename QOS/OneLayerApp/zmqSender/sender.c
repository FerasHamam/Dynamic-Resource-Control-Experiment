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
#include <fftw3.h>
#include <math.h>
#include <fcntl.h>
#include <sys/time.h>

// General Parameters (ZMQ)
#define BASE_PORT 5555
#define SHARED_IP "10.10.10.4"
#define DEDICATED_IP "10.10.10.8"
#define NUM_STEPS 100
#define CHUNK_SIZE 16 * 1024 * 1024
#define DIRECTORY "../data/"

// Bandwidth prediction parameters
#define BW_MAX 370.0
#define BW_MIN 0.0
#define k1 (80.0 / (BW_MAX - BW_MIN))
#define b1 (20.0 - (k1 * BW_MIN))
#define TIME_WINDOW 25.0
#define BANDWIDTH (400)
#define MONITOR_SIZE 10000

// Global shared resources
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
volatile bool stop_threads = false;
void *context;
int step_aug = 0;
double elapsed_seconds = 0;
int predictions_counter = 0;
struct timeval program_start_time;

typedef struct
{
    char **filenames;
    int num_files;
    int thread_index;
} ThreadArgs;

typedef struct
{
    double time;
    double rate;
} Prediction;

Prediction *predictions = NULL;
int prediction_size = 0;

void sleep_ms(double milliseconds)
{
    struct timespec ts;
    ts.tv_sec = (long)(milliseconds / 1000.0);
    ts.tv_nsec = (long)((milliseconds - ((long)(milliseconds / 1000.0) * 1000.0)) * 1000000.0);
    nanosleep(&ts, NULL);
}

double get_elapsed_seconds()
{
    struct timeval current_time;
    gettimeofday(&current_time, NULL);

    // Calculate difference with microsecond precision
    double seconds_diff = (double)(current_time.tv_sec - program_start_time.tv_sec);
    double microseconds_diff = (double)(current_time.tv_usec - program_start_time.tv_usec) / 1000000.0;

    return seconds_diff + microseconds_diff;
}

// Function to read bandwidth predictions from file
int read_predictions(FILE *file)
{
    char line[25];
    int count = 0;

    printf("Reading predictions\n");
    while (fgets(line, sizeof(line), file) != NULL && count < MONITOR_SIZE)
    {
        char *time_str = strtok(line, ",");
        char *rate_str = strtok(NULL, ",");

        if (time_str != NULL && rate_str != NULL)
        {
            predictions[count].time = atof(time_str);
            predictions[count].rate = atof(rate_str);
            count++;
        }
    }
    predictions_counter = count;
    return count;
}

// Thread function to calculate congestion
void *calculate_congestion(void *arg)
{
    printf("Starting congestion thread\n");
    predictions = malloc(sizeof(Prediction) * MONITOR_SIZE);
    if (!predictions)
    {
        perror("Failed to allocate memory for predictions");
        return NULL;
    }

    char *prediction_filename = "predictions.txt";
    while (!stop_threads)
    {
        sleep(1200);
        FILE *prediction_file = fopen(prediction_filename, "r");
        if (!prediction_file)
        {
            char command[1024];
            snprintf(command, sizeof(command), "%s %s", "python3", "../scripts/fft.py");
            int ret = system(command);
            if (ret != 0)
            {
                fprintf(stderr, "Error executing command: %s\n", command);
                break;
            }

            // Wait for file to be created
            for (int i = 0; i < 10 && !prediction_file; i++)
            {
                sleep(1);
                prediction_file = fopen(prediction_filename, "r");
            }

            if (!prediction_file)
            {
                fprintf(stderr, "Failed to open prediction file\n");
                continue;
            }
        }

        pthread_mutex_lock(&mutex);
        prediction_size = read_predictions(prediction_file);
        pthread_mutex_unlock(&mutex);

        printf("Read %d predictions\n", prediction_size);
        fclose(prediction_file);
        printf("Processed congestion prediction until step %d\n", step_aug + 1);
    }

    printf("Exiting congestion thread\n");
    return NULL;
}

void log_chunk_transfer(double transfer_rate_mbps, int step)
{
    pid_t pid = fork();

    if (pid < 0)
    {
        // Fork failed
        perror("Fork failed for logging");
        return;
    }
    else if (pid == 0)
    {
        FILE *log_file;
        // double elapsed_time = get_elapsed_seconds();
        double elapsed_time = step * 60.0; // Assuming each step takes 40 seconds
        if (access("log.txt", F_OK) != 0)
        {
            log_file = fopen("log.txt", "w");
            if (log_file == NULL)
            {
                perror("Error creating log.txt");
                exit(EXIT_FAILURE);
            }
            fprintf(log_file, "Time(s),File,Chunk Size(bytes),Transfer Rate(Mbps)\n");
        }
        else
        {
            log_file = fopen("log.txt", "a");
            if (log_file == NULL)
            {
                perror("Error opening log.txt");
                exit(EXIT_FAILURE);
            }
        }

        fprintf(log_file, "%.2f,%.2f\n",
                elapsed_time,
                transfer_rate_mbps);

        fclose(log_file);
        exit(EXIT_SUCCESS);
    }

    return;
}

// Connect to socket
void connect_socket(void **socket, int thread_index)
{
    char bind_address[50];
    if (thread_index == 0)
    {
        snprintf(bind_address, sizeof(bind_address), "tcp://%s:%d", DEDICATED_IP, BASE_PORT + thread_index);
    }
    else
    {
        snprintf(bind_address, sizeof(bind_address), "tcp://%s:%d", SHARED_IP, BASE_PORT + thread_index);
    }

    *socket = zmq_socket(context, ZMQ_PAIR);
    zmq_connect(*socket, bind_address);
    printf("Connected to port %d\n", BASE_PORT + thread_index);
}

// Send data chunk
void send_data_chunk(void *socket, char *message, size_t size)
{
    zmq_msg_t msg;
    zmq_msg_init_size(&msg, size);
    memcpy(zmq_msg_data(&msg), message, size);
    zmq_msg_send(&msg, socket, 0);
    zmq_msg_close(&msg);
}

// Receive data chunk
void recv_data_chunk(void *socket, char **data, size_t *size)
{
    zmq_msg_t msg;
    zmq_msg_init(&msg);
    zmq_msg_recv(&msg, socket, 0);
    *size = zmq_msg_size(&msg);
    *data = malloc(*size);
    if (!*data)
    {
        perror("Failed to allocate memory for data chunk");
        *size = 0;
        zmq_msg_close(&msg);
        return;
    }
    memcpy(*data, zmq_msg_data(&msg), *size);
    zmq_msg_close(&msg);
}

// Open file for reading
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

// Get file size
size_t get_file_size(const char *filename)
{
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s%s", DIRECTORY, filename);
    FILE *file = fopen(filepath, "rb");
    if (!file)
    {
        perror("Error getting file size");
        return 0;
    }
    fseek(file, 0, SEEK_END);
    size_t size = ftell(file);
    fclose(file);
    return size;
}

double get_file_percentage(size_t file_size)
{
    pthread_mutex_lock(&mutex);
    double percentage = 100;
    if (predictions == NULL || prediction_size == 0)
    {
        pthread_mutex_unlock(&mutex);
        return percentage;
    }

    // Convert file size to Mbits for consistent units
    double file_size_Mbits = file_size * 8.0 / 1000000.0;

    // Calculate average bandwidth over the next TIME_WINDOW seconds
    double total_bandwidth = predictions[step_aug % predictions_counter].rate;

    // Calculate average bandwidth over this window
    double avg_predicted_bandwidth = total_bandwidth;
    pthread_mutex_unlock(&mutex);

    // Calculate how much data we can transfer in TIME_WINDOW seconds
    // with the predicted bandwidth (in Mbits)
    // double transfer_capacity = avg_predicted_bandwidth * TIME_WINDOW;

    // Calculate what percentage of the file that representsa
    printf("avg BW: %.2f\n", avg_predicted_bandwidth);
    printf("step_aug: %d, predictions_counter: %d, %d\n", step_aug, predictions_counter, step_aug % predictions_counter);
    percentage = (avg_predicted_bandwidth / BW_MAX) * 100.0;

    // Cap at 100% (can't transfer more than the entire file)
    if (percentage > 100.0)
    {
        percentage = 100.0;
    }

    // For debugging
    printf("Avg predicted bandwidth: %.2f Mbps\n", avg_predicted_bandwidth);
    // printf("File size: %.2f Mbits, Transfer capacity: %.2f Mbits\n",
    //        file_size_Mbits, transfer_capacity);
    printf("Percentage to send: %.2f%%\n", percentage);

    return percentage;
}

// Main thread function to send data
void *send_data(void *arg)
{
    ThreadArgs *args = (ThreadArgs *)arg;
    char **filenames = args->filenames;
    int num_files = args->num_files;
    int thread_index = args->thread_index;
    void *sender;

    connect_socket(&sender, thread_index);
    int step = 0;

    while (step < NUM_STEPS && !stop_threads)
    {

        if (thread_index == 1)
        {
            elapsed_seconds = get_elapsed_seconds();
            printf("Elapsed time: %.2f seconds\n", elapsed_seconds);
        }

        double dynamic_progress_threshold = 100;
        if (thread_index == 1)
        {
            dynamic_progress_threshold = get_file_percentage(get_file_size(filenames[0]) * 3);
        }

        struct timeval start_step, end_step;
        gettimeofday(&start_step, NULL);
        double data_size = 0;
        double transfer_rate_mbps = 0;
        double count = 0;
        for (int i = 0; i < num_files; i++)
        {

            send_data_chunk(sender, filenames[i], strlen(filenames[i]) + 1);

            FILE *file;
            if (open_file(&file, filenames[i]))
            {
                zmq_close(sender);
                return NULL;
            }

            // Send file data in chunks
            size_t file_size = get_file_size(filenames[i]);
            char *buffer = (char *)malloc(file_size);
            if (!buffer)
            {
                perror("Failed to allocate memory for buffer");
                fclose(file);
                zmq_close(sender);
                return NULL;
            }
            // Read a percentage of the file based on dynamic_progress_threshold
            size_t point_size = 8;
            size_t total_points = file_size / point_size;
            size_t points_to_send = (size_t)((dynamic_progress_threshold / 100.0) * total_points);
            size_t bytes_to_read = points_to_send * point_size;
            size_t bytes_actually_read = fread(buffer, 1, bytes_to_read, file);

            fclose(file);

            printf("File: %s, Size: %zu bytes (%zu points)\n",
                   filenames[i], file_size, total_points);
            printf("Sending %zu points (%zu bytes, %.2f%%)\n",
                   points_to_send, bytes_to_read, (float)points_to_send / total_points * 100);
            send_data_chunk(sender, buffer, bytes_actually_read);

            // Receive time taken from receiver
            char *time_data;
            size_t time_data_size;
            recv_data_chunk(sender, &time_data, &time_data_size);
            if (thread_index == 1)
                printf("Received time taken: %s\n", time_data);

            if (time_data && time_data_size > 0)
            {
                double chunk_transfer_time = atof(time_data);
                transfer_rate_mbps += ((file_size * 8) / 1000000.0) / chunk_transfer_time;
                count++;
                free(time_data);
            }

            free(buffer);

            // // Send empty chunk to signal end of file
            // send_data_chunk(sender, "", 0);

            // Determine alert message
            char *message = (step == NUM_STEPS - 1 && i == num_files - 1) ? "0" : (i < num_files - 1) ? "1"
                                                                                                      : "2";
            send_data_chunk(sender, message, strlen(message) + 1);

            // Process acknowledgment if needed
            if (*message != '1')
            {
                char *ack_message;
                size_t size;
                recv_data_chunk(sender, &ack_message, &size);
                if (ack_message)
                {
                    printf("Received ack: %s\n", ack_message);
                    free(ack_message);
                }
            }
        }
        // log the time taken for the file transfer
        if (thread_index == 1)
        {
            printf("transfer rate: %.2f, count: %.2f, rate: %.2f\n", transfer_rate_mbps, count, transfer_rate_mbps / count);
            log_chunk_transfer(transfer_rate_mbps / count, step);
            printf("\n--- Step %d completed ---\n", step);
            pthread_mutex_lock(&mutex);
            step_aug++;
            pthread_mutex_unlock(&mutex);
        }

        // Handle sleep between steps
        double send_time = get_elapsed_seconds() - elapsed_seconds;
        double remaining = 60.0 - send_time;

        if (remaining > 0)
        {
            printf("[Thread %d] Sleeping for %.2f seconds\n", thread_index, remaining);
            sleep_ms(remaining * 1000.0);
        }
        step++;
    }

    zmq_close(sender);
    return NULL;
}

int main()
{
    gettimeofday(&program_start_time, NULL);
    printf("Starting Sender...\n");

    // Initialize ZeroMQ context
    context = zmq_ctx_new();

    // Create threads
    pthread_t thread1, thread2, congestion_thread;
    ThreadArgs args1 = {.filenames = (char *[]){"reduced_data_xgc_16.bin"}, .num_files = 1, .thread_index = 0};
    ThreadArgs args2 = {.filenames = (char *[]){"delta_r_xgc_o.bin", "delta_z_xgc_o.bin", "delta_xgc_o.bin"},
                        .num_files = 3,
                        .thread_index = 1};

    if (pthread_create(&thread1, NULL, send_data, &args1) != 0)
    {
        fprintf(stderr, "Error: Failed to create send file thread 1\n");
        return EXIT_FAILURE;
    }

    if (pthread_create(&thread2, NULL, send_data, &args2) != 0)
    {
        fprintf(stderr, "Error: Failed to create send file thread 2\n");
        return EXIT_FAILURE;
    }

    if (pthread_create(&congestion_thread, NULL, calculate_congestion, NULL) != 0)
    {
        fprintf(stderr, "Error: Failed to create congestion thread\n");
        return EXIT_FAILURE;
    }

    // Wait for threads to finish
    pthread_join(thread1, NULL);
    pthread_join(thread2, NULL);

    // Signal all threads to stop
    stop_threads = true;

    pthread_join(congestion_thread, NULL);

    // Clean up resources
    if (predictions)
    {
        free(predictions);
    }

    pthread_mutex_destroy(&mutex);
    zmq_ctx_destroy(context);

    printf("Transfer rates have been logged to log.txt\n");
    return EXIT_SUCCESS;
}