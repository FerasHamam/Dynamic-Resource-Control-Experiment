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

#define BASE_PORT 4444
#define SHARED_IP "IP"
#define DETICATED_IP "IP"
#define NUM_STEPS 30
#define MONITOR_SIZE 100000
#define LINK_BANDWIDTH 200.0
#define CHUNK_SIZE 1024 * 1024

typedef enum
{
    LOW,
    HIGH
} SocketPriority;

typedef enum
{
    REDUCED,
    AUG
} FilesType;

typedef struct
{
    char **filenames;
    int num_files;
    int thread_index;
} ThreadArgs;

// Global shared resources
pthread_mutex_t bandwidth_mutex = PTHREAD_MUTEX_INITIALIZER;
double time_taken[MONITOR_SIZE] = {0};
size_t bytes_sent[MONITOR_SIZE] = {0};
int aug_size_prediciton_based_on_congestion[NUM_STEPS];
int monitor_index = 0;
int step_aug = 0;
volatile bool stop_congestion_thread = false;
void *context;

// Apply FFT to detect periodic patterns
void apply_fft(double *input, int n, double *output)
{
    fftw_complex *in, *out;
    fftw_plan plan;

    // Allocate memory for FFT input and output
    in = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * n);
    out = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * n);

    // Prepare input for FFT (real values only)
    for (int i = 0; i < n; i++)
    {
        in[i][0] = input[i]; // Real part
        in[i][1] = 0.0;      // Imaginary part
    }

    // Create FFT plan
    plan = fftw_plan_dft_1d(n, in, out, FFTW_FORWARD, FFTW_ESTIMATE);

    // Execute FFT
    fftw_execute(plan);

    // Calculate magnitude of frequencies
    for (int i = 0; i < n; i++)
    {
        output[i] = sqrt(out[i][0] * out[i][0] + out[i][1] * out[i][1]);
    }

    // Free memory
    fftw_destroy_plan(plan);
    fftw_free(in);
    fftw_free(out);
}

// Helper function to calculate the threshold dynamically
double calculate_threshold(double *frequencies, int size)
{
    // Calculate mean and standard deviation of frequencies
    double sum = 0.0, sum_sq = 0.0;
    int valid_count = 0;
    for (int i = 1; i < size / 2; i++)
    {
        sum += frequencies[i];
        sum_sq += frequencies[i] * frequencies[i];
        valid_count++;
    }
    double mean = sum / valid_count;
    double variance = (sum_sq / valid_count) - (mean * mean);
    double stddev = sqrt(variance);

    // Calculate threshold (adjust factors as needed)
    return mean + 1.5 * stddev;
}

void calculate_congestion(void *arg)
{
    double rate[MONITOR_SIZE] = {0};        // Transfer rate at each step
    double frequencies[MONITOR_SIZE] = {0}; // Frequency components
    int last_processed_step = 0;
    while (!stop_congestion_thread)
    {
        int copy_of_monitor_index = 0;
        pthread_mutex_lock(&bandwidth_mutex);
        if (step_aug <= last_processed_step || step_aug % 3 != 0)
        {
            pthread_mutex_unlock(&bandwidth_mutex);
            sleep(5);
            continue;
        }

        last_processed_step = step_aug;

        int sum_rate = 0;
        for (int i = 0; i < monitor_index; i++)
        {
            if (time_taken[i] == 0)
            {
                continue;
            }
            rate[i] = (time_taken[i] > 0.0) ? (double)bytes_sent[i] / time_taken[i] : 0;
            time_taken[i] = bytes_sent[i] = 0;
        }
        copy_of_monitor_index = monitor_index;
        monitor_index = 0;
        pthread_mutex_unlock(&bandwidth_mutex);

        // Apply FFT to detect patterns
        apply_fft(rate, copy_of_monitor_index, frequencies);

        // Find indices of dominant frequencies
        int dominant_indices[MONITOR_SIZE];
        int num_dominant_indices = 0;
        double threshold = calculate_threshold(frequencies, copy_of_monitor_index);
        for (int i = 1; i < copy_of_monitor_index / 2; i++)
        {
            if (frequencies[i] > threshold)
            {
                dominant_indices[num_dominant_indices++] = i;
            }
        }

        // Calculate average rate during periods corresponding to dominant frequencies
        double predicted_rate = 0.0;
        if (num_dominant_indices > 0)
        {
            for (int i = 0; i < num_dominant_indices; i++)
            {
                // Estimate period (inverse of frequency, adjust as needed)
                double period = copy_of_monitor_index / (double)dominant_indices[i];
                // Calculate average rate within the estimated period
                int start_index = fmax(0, dominant_indices[i] - (int)(period / 2));
                int end_index = fmin(dominant_indices[i] + (int)(period / 2), copy_of_monitor_index - 1);
                double period_rate_sum = 0.0;
                int period_count = 0;
                for (int j = start_index; j <= end_index; ++j)
                {
                    period_rate_sum += rate[j];
                    period_count++;
                }
                if (period_count > 0)
                {
                    predicted_rate += period_rate_sum / period_count;
                }
            }
            predicted_rate /= num_dominant_indices; // Average over dominant frequencies
        }

        // Calculate congestion
        double throughput = predicted_rate * 8.0 / 1e6; // Convert to Mbps
        printf("Predicted rate: %.2f Mbps\n", throughput);
        double congestion = (1.0 - (throughput / (LINK_BANDWIDTH))) * 100;
        int dynamic_progress_threshold = 100;
        if (congestion > 20)
        {
            dynamic_progress_threshold = 100 - (congestion - 20);
        }

        pthread_mutex_lock(&bandwidth_mutex);
        for (int i = step_aug + 1; i < step_aug + 5; i++)
        {
            if (i < NUM_STEPS)
            {
                aug_size_prediciton_based_on_congestion[i] = dynamic_progress_threshold;
            }
        }
        pthread_mutex_unlock(&bandwidth_mutex);
    }
    pthread_exit(NULL);
}

bool open_files(char **filenames, int num_files, FILE **files, double *file_sizes, FilesType files_type)
{
    const char *directory = "../data/";
    for (int i = 0; i < num_files; i++)
    {
        char filepath[256];
        snprintf(filepath, sizeof(filepath), "%s%s", directory, filenames[i]);
        files[i] = fopen(filepath, "rb");
        if (!files[i])
        {
            fprintf(stderr, "Failed to open file %s\n", filenames[i]);
            return false;
        }
        fseek(files[i], 0, SEEK_END);
        file_sizes[i] = ftell(files[i]);
        fseek(files[i], 0, SEEK_SET);
    }
    return true;
}

void close_files(FILE **files, int num_files, FilesType files_type)
{
    for (int i = 0; i < num_files; i++)
    {
        fclose(files[i]);
    }
}

void *connect_socket(int port, int thread_index)
{
    void *sender = zmq_socket(context, ZMQ_PAIR);
    char bind_address[50];
    if (thread_index == 0)
        snprintf(bind_address, sizeof(bind_address), "tcp://%s:%d", DETICATED_IP, BASE_PORT + thread_index);
    else
        snprintf(bind_address, sizeof(bind_address), "tcp://%s:%d", SHARED_IP, BASE_PORT + thread_index);
    if (zmq_connect(sender, bind_address) != 0)
    {
        fprintf(stderr, "Failed to connect to client: %s\n", zmq_strerror(zmq_errno()));
        return NULL;
    }
    printf("Connected to port %d\n", BASE_PORT + thread_index);
    return sender;
}

void close_socket(void *socket)
{
    zmq_close(socket);
}

void send_data_chunk(void *socket, char *message, size_t size)
{
    zmq_msg_t msg;
    zmq_msg_init_size(&msg, size);
    zmq_msg_init_data(&msg, message, size, NULL, NULL);
    zmq_msg_send(&msg, socket, 0);
    zmq_msg_close(&msg);
}

void recv_str_data_chunk(void *socket, char **data, size_t *size)
{
    zmq_msg_t msg;
    zmq_msg_init(&msg);
    zmq_msg_recv(&msg, socket, 0);
    *size = zmq_msg_size(&msg);
    *data = malloc(*size);
    memcpy(*data, zmq_msg_data(&msg), *size);
    zmq_msg_close(&msg);
}

void recv_double_data_chunk(void *socket, double *double_data)
{
    zmq_msg_t msg;
    zmq_msg_init(&msg);
    zmq_msg_recv(&msg, socket, 0);
    *double_data = *(double *)zmq_msg_data(&msg); // Convert the data to double
    zmq_msg_close(&msg);
}

void *send_data(void *arg)
{
    // Read args
    ThreadArgs *args = (ThreadArgs *)arg;
    char **filenames = args->filenames;
    int num_files = args->num_files;
    int thread_index = args->thread_index;

    void *sender = connect_socket(BASE_PORT, thread_index);
    int step = 0;
    while (step < NUM_STEPS)
    {

        pthread_mutex_lock(&bandwidth_mutex);
        double dynamic_progress = (thread_index == 1) ? aug_size_prediciton_based_on_congestion[step] : 100;
        pthread_mutex_unlock(&bandwidth_mutex);
        // Send all filenames at in consecutive order
        for (int j = 0; j < num_files; j++)
        {
            send_data_chunk(sender, filenames[j], strlen(filenames[j]) + 1);
        }
        // End of filenames
        send_data_chunk(sender, "", 0);

        // Open file for reading
        FILE *files[num_files];
        bool read_files[num_files];
        double total_files_size[num_files];
        open_files(filenames, num_files, files, total_files_size, thread_index ? AUG : REDUCED);

        // Send file data
        int file_index = 0;
        int num_sent_files = 0;
        size_t chunk_size = CHUNK_SIZE;
        double bytes_sent_per_file[num_files];
        for (int i = 0; i < num_files; i++)
        {
            bytes_sent_per_file[i] = 0;
            read_files[i] = false;
        }
        int iter = 0;
        while (num_sent_files < num_files)
        {
            char *buffer = (char *)malloc(chunk_size);
            size_t bytes_read = fread(buffer, 1, chunk_size, files[file_index]);
            bytes_sent_per_file[file_index] += bytes_read;
            fseek(files[file_index], bytes_sent_per_file[file_index], SEEK_SET);
            // Send the file data
            if (bytes_read > 0)
            {
                send_data_chunk(sender, buffer, bytes_read);
                // Receive Log info
                double log_message;
                recv_double_data_chunk(sender, &log_message);

                // calculate bandwidth based on time taken in Mbps
                if (thread_index == 1)
                {
                    pthread_mutex_lock(&bandwidth_mutex);
                    time_taken[monitor_index % MONITOR_SIZE] = log_message;
                    bytes_sent[monitor_index % MONITOR_SIZE] = bytes_read;
                    monitor_index++;
                    pthread_mutex_unlock(&bandwidth_mutex);
                }

                // Check if the file has been completely sent based on progress
                if (thread_index == 1)
                {
                    double progress = ((double)bytes_sent_per_file[file_index] / (double)total_files_size[file_index]) * 100.0f;
                    if (progress >= dynamic_progress)
                    {
                        // Send the close message with 0 bytes
                        printf("File: %s, Progress: %.2f%%\n", filenames[file_index], progress);
                        send_data_chunk(sender, "", 0);
                        read_files[file_index] = true;
                        num_sent_files++;
                    }
                }
                iter++;
                if ((read_files[file_index]) || (iter % 1 == 0))
                {
                    file_index = (file_index + 1) % num_files;
                    iter = 0;
                }
                free(buffer);
            }
            else
            {
                // Send the close message with 0 bytes
                send_data_chunk(sender, "", 0);
                read_files[file_index] = true;
                file_index = (file_index + 1) % num_files;
                num_sent_files++;
            }
        }
        close_files(files, num_files, thread_index ? AUG : REDUCED);
        // Alert message
        // if 0 that means the port is complete and no more steps,
        // if 1 move to the next step by incrementing step
        bool is_port_complete = (step == NUM_STEPS - 1);
        send_data_chunk(sender, (is_port_complete) ? "0" : "1", 2);
        char *ack_message;
        size_t ack_size;
        recv_str_data_chunk(sender, &ack_message, &ack_size);
        printf("Received ack message: %s\n", ack_message);
        // Increment step
        step++;
        if (thread_index == 1)
        {
            pthread_mutex_lock(&bandwidth_mutex);
            step_aug = step_aug + 1;
            pthread_mutex_unlock(&bandwidth_mutex);
        }
    }
    close_socket(sender);
    pthread_exit(NULL);
    return NULL;
}

int main()
{
    printf("Starting Sender...\n");
    for (int i = 0; i < NUM_STEPS; i++)
    {
        aug_size_prediciton_based_on_congestion[i] = 100;
    }

    context = zmq_ctx_new();
    pthread_t reduced_thread, aug_thread, congestion_thread;

    if (pthread_create(&congestion_thread, NULL, calculate_congestion, NULL) != 0)
    {
        fprintf(stderr, "Error: Failed to create congestion thread\n");
        exit(EXIT_FAILURE);
    }

    // Filenames to be sent
    char *reduced_filenames[] = {"reduced_data_xgc_16.bin"};
    char *aug_filenames[] = {"delta_r_xgc_o.bin", "delta_z_xgc_o.bin", "delta_xgc_o.bin"};

    // Create Reduced thread arguments
    ThreadArgs reduced_args;
    reduced_args.filenames = reduced_filenames;
    reduced_args.num_files = 1;
    reduced_args.thread_index = 0;

    // Create a thread for sending Reduced files
    if (pthread_create(&reduced_thread, NULL, send_data, &reduced_args) != 0)
    {
        fprintf(stderr, "Error: Failed to create send file thread\n");
        return EXIT_FAILURE;
    }

    // Create Aug thread arguments
    ThreadArgs aug_args;
    aug_args.filenames = aug_filenames;
    aug_args.num_files = 3;
    aug_args.thread_index = 1;

    // Create a thread for sending Aug files
    if (pthread_create(&aug_thread, NULL, send_data, &aug_args) != 0)
    {
        fprintf(stderr, "Error: Failed to create send file thread\n");
        return EXIT_FAILURE;
    }

    // Wait for the send file thread to finish
    pthread_join(reduced_thread, NULL);
    pthread_join(aug_thread, NULL);

    stop_congestion_thread = true;
    pthread_join(congestion_thread, NULL);
    // Clean up ZeroMQ context
    zmq_ctx_destroy(context);

    return EXIT_SUCCESS;
}
