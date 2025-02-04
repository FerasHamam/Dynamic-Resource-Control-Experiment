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
#define SHARED_IP "10.10.10.3"
#define DETICATED_IP "10.10.10.6"
#define NUM_STEPS 50
#define CHUNK_SIZE 1024 * 1024
#define DIRECTORY "../data/"

// Bandwidth prediction parameters
#define BW_MAX 180
#define BW_MIN 45
#define k1  (100.0 / (BW_MAX - BW_MIN))
#define b1  (-100.0 * BW_MIN / (BW_MAX - BW_MIN))
#define BANDWIDTH (200 * 1000000)
#define MONITOR_SIZE 100000
#define step_prediction 10

// Global shared resources
pthread_mutex_t bandwidth_mutex = PTHREAD_MUTEX_INITIALIZER;
int step_aug = 0;
volatile bool stop_congestion_thread = false;
void *context;
int prediction_size = 0;
double elpased_seconds = 0;
bool stop_logging = false;
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

void get_mbps_rate(const char *interface)
{
    pid_t pid = fork();
    if (pid == -1)
    {
        perror("fork failed");
        return;
    }

    if (pid == 0)
    {
        sleep(1);
        FILE *log = fopen("log.txt", "w");
        if (!log)
        {
            perror("Unable to open log file");
            return;
        }

        char path[256];
        sprintf(path, "/sys/class/net/%s/statistics/tx_bytes", interface);

        long long tx_bytes_1, tx_bytes_2, tx_diff;
        double rate;
        time_t current_time;
        struct tm *time_info;
        char time_str[9];

        // Initial tx_bytes
        FILE *fp = fopen(path, "r");
        fscanf(fp, "%lld", &tx_bytes_1);
        fclose(fp);

        while (!stop_congestion_thread)
        {
            sleep(2);
            current_time = time(NULL);
            if (!stop_logging)
            {
                fp = fopen(path, "r");
                fscanf(fp, "%lld", &tx_bytes_2);
                fclose(fp);
                tx_diff = tx_bytes_2 - tx_bytes_1;
                rate = ((double)tx_diff / 2) * 8 / 1000000.0;
                time_info = localtime(&current_time);
                strftime(time_str, sizeof(time_str), "%H:%M:%S", time_info);
                fprintf(log, "%s,%.2f Mbps\n", time_str, rate);
                fflush(log);
                tx_bytes_1 = tx_bytes_2;
            }
        }
        fclose(log);
    }
}

int read_predictions(FILE *file)
{
    char line[25];
    int count = 0;

    printf("Reading predictions\n");
    // Read each line from the file
    while (fgets(line, sizeof(line), file) != NULL)
    {
        // Tokenize the line by the comma
        char *time_str = strtok(line, ",");
        char *rate_str = strtok(NULL, ",");
        if (time_str != NULL && rate_str != NULL)
        {
            // Store the time and rate in the array
            predictions[count].time = atof(time_str);
            predictions[count].rate = atof(rate_str);
            count++;
        }
    }
    return count; // Return the number of predictions read
}

void *calculate_congestion(void *arg)
{
    int last_processed_step = 0;
    predictions = malloc(sizeof(Prediction) * MONITOR_SIZE);
    char *prediction_filename = "predictions.txt";
    while (!stop_congestion_thread)
    {
        pthread_mutex_lock(&bandwidth_mutex);
        if (step_aug <= last_processed_step || (step_aug % step_prediction != 0) || stop_logging)
        {
            pthread_mutex_unlock(&bandwidth_mutex);
            sleep(5);
            continue;
        }
        pthread_mutex_unlock(&bandwidth_mutex);

        FILE *prediciton_file = fopen(prediction_filename, "r");
        if (!prediciton_file)
        {
            char command[1024];
            snprintf(command, sizeof(command), "%s %s --prediction %d", "python3", "../scripts/fft.py", step_aug);
            int ret = system(command); // Execute Python script
            if (ret != 0)
            {
                fprintf(stderr, "Error executing command: %s\n", command);
                break;
            }
            while (!prediciton_file)
            {
                sleep(1);
                prediciton_file = fopen("prediction.txt", "r");
            }
            pthread_mutex_lock(&bandwidth_mutex);
            prediction_size = read_predictions(prediciton_file);
            pthread_mutex_unlock(&bandwidth_mutex);
        }
        remove("log.txt");
        fclose(prediciton_file);
        remove(prediction_filename);
        printf("Processed congestion prediction for step %d\n", step_aug);
        printf("Prediction size: %d\n", prediction_size);
        pthread_mutex_lock(&bandwidth_mutex);
        last_processed_step = NUM_STEPS+1;
        //last_processed_step = step_aug;
        stop_logging = true;
        pthread_mutex_unlock(&bandwidth_mutex);
    }
    printf("Exiting congestion thread\n");
    pthread_exit(NULL);
}

void connect_socket(void **socket, int thread_index)
{
    char bind_address[50];
    if (thread_index == 0)
        snprintf(bind_address, sizeof(bind_address), "tcp://%s:%d", DETICATED_IP, BASE_PORT + thread_index);
    else
        snprintf(bind_address, sizeof(bind_address), "tcp://%s:%d", SHARED_IP, BASE_PORT + thread_index);
    *socket = zmq_socket(context, ZMQ_PAIR);
    int socket_fd = (uintptr_t)*socket; // Cast void* to int
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

double get_file_percentage(size_t file_size)
{
    pthread_mutex_lock(&bandwidth_mutex);
    double percentage = 100;
    if (predictions == NULL || prediction_size == 0)
    {
        pthread_mutex_unlock(&bandwidth_mutex);
        return percentage;
    }

    double file_size_bits = file_size * 8.0;
    double bandwidth_bps = BANDWIDTH;
    double full_bandwidth_estimated_time = file_size_bits / bandwidth_bps;
    double elapsed = elpased_seconds;

    if (elapsed > predictions[prediction_size - 1].time)
        elapsed -= predictions[prediction_size - 1].time;

    double total_bandwidth = 0.0;
    double time_remaining = full_bandwidth_estimated_time; // Time left to cover
    double total_time_covered = 0.0;                       // Total time we've covered so far
    int count = 0;

    // Find the starting index in the predictions array based on elapsed time int start_index = 0;
    int start_index = 0;
    for (int i = 0; i < prediction_size; i++)
    {
        printf("Prediction rate: %.2f\n", predictions[i].rate);
    }

    for (int i = 0; i < prediction_size; i++)
    {
        if (predictions[i].time >= elapsed)
        {
            start_index = i;
            break;
        }
    }
    while (total_time_covered < full_bandwidth_estimated_time)
    {
        int index = (start_index + count) % prediction_size;
        if (total_time_covered <= full_bandwidth_estimated_time)
        {
            total_bandwidth += predictions[index].rate;
            //printf("Predicted Bandwidth: %.2f Mbps\n", predictions[index].rate);
            total_time_covered += 2;
        }
        count++;
    }
    pthread_mutex_unlock(&bandwidth_mutex);

    double avg_predicted_bandwidth = total_bandwidth / count;
    if (avg_predicted_bandwidth > BW_MAX)
    {
        percentage = 100;
    }
    else if (avg_predicted_bandwidth >= BW_MIN)
    {
        percentage = k1 * avg_predicted_bandwidth + b1;
    }
    else
    {
        avg_predicted_bandwidth = 0;
    }

    printf("Predicted Bandwidth: %.2f Mbps\n", avg_predicted_bandwidth);
    printf("Percentage: %.2f\n", percentage);
    return percentage;
}

size_t get_file_size(const char *filename)
{
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s%s", DIRECTORY, filename);
    FILE *file = fopen(filepath, "rb");
    if (!file)
    {
        perror("Error Getting file size");
        return 0;
    }
    fseek(file, 0, SEEK_END);
    size_t size = ftell(file);
    fclose(file);
    return size;
}

void *send_data(void *arg)
{
    // Read args
    ThreadArgs *args = (ThreadArgs *)arg;
    char **filenames = args->filenames;
    int num_files = args->num_files;
    int thread_index = args->thread_index;
    time_t start_time = time(NULL);
    void *sender;
    connect_socket(&sender, thread_index);
    int step = 0;
    while (step < NUM_STEPS)
    {
        time_t current_time = time(NULL);
        elpased_seconds = difftime(current_time, start_time);
        double dynamic_progress_threshold = 100;
        if (thread_index == 1)
        {
            dynamic_progress_threshold = get_file_percentage(get_file_size(filenames[0]) * 3);
        }
        // Send file
        for (int i = 0; i < num_files; i++)
        {
            size_t file_size = get_file_size(filenames[i]);
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
            size_t sent_bytes = 0;
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
                sent_bytes += bytes_read;
                if (thread_index == 1)
                {
                    double progress = (sent_bytes / (double)file_size) * 100;
                    if (progress >= dynamic_progress_threshold)
                    {
                        break;
                    }
                }
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
            char *ack_message;
            size_t size;
            recv_data_chunk(sender, &ack_message, &size);
            printf("Received ack message: %s\n", ack_message);
        }
        // Increment step
        step++;
        if (thread_index == 1)
        {
            pthread_mutex_lock(&bandwidth_mutex);
            step_aug++;
            pthread_mutex_unlock(&bandwidth_mutex);
        }
    }

    zmq_close(sender);
    pthread_exit(NULL);
    return NULL;
}

int main()
{
    printf("Starting Sender...\n");
    context = zmq_ctx_new();
    pthread_t thread1, thread2, congestion_thread;

    if (pthread_create(&congestion_thread, NULL, calculate_congestion, NULL) != 0)
    {
        fprintf(stderr, "Error: Failed to create congestion thread\n");
        exit(EXIT_FAILURE);
    }

    // Create thread arguments 1
    ThreadArgs args1;
    char *filenames1[] = {"reduced_data_xgc_16.bin"};
    args1.filenames = filenames1;
    args1.num_files = 1;
    args1.thread_index = 0;
    if (pthread_create(&thread1, NULL, send_data, &args1) != 0)
    {
        fprintf(stderr, "Error: Failed to create send file thread\n");
        return EXIT_FAILURE;
    }

    // Create thread arguments 2
    ThreadArgs args2;
    char *filenames2[] = {"delta_r_xgc_o.bin", "delta_z_xgc_o.bin", "delta_xgc_o.bin"};
    args2.filenames = filenames2;
    args2.num_files = 3;
    args2.thread_index = 1;

    if (pthread_create(&thread2, NULL, send_data, &args2) != 0)
    {
        fprintf(stderr, "Error: Failed to create send file thread\n");
        return EXIT_FAILURE;
    }

    get_mbps_rate("enp7s0");

    // Wait for threads to finish
    pthread_join(thread1, NULL);
    pthread_join(thread2, NULL);

    stop_congestion_thread = true;
    pthread_join(congestion_thread, NULL);
    // Clean up ZeroMQ context
    zmq_ctx_destroy(context);

    return EXIT_SUCCESS;
}
