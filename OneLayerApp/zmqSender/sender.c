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
#define CLIENT_IP "RECEIVER_IP"
#define NUM_STEPS 1
#define BANDWIDTH_SIZE 60
#define LINK_BANDWIDTH 200.0

typedef enum
{
    LOW,
    HIGH
} SocketPriority;

// Global shared resources
pthread_mutex_t bandwidth_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
double bandwidths_reduced[BANDWIDTH_SIZE];
double bandwidths_aug[BANDWIDTH_SIZE];
int reduced_index = 0;
int aug_index = 0;
volatile double dynamic_progress_threshold = 100.0;
volatile double max_progress_per_step = 0;
volatile bool stop_congestion_thread = false;
volatile int active_file_index = 0;

void *context;
typedef struct
{
    char **filenames;
    int num_files;
    int thread_index;
} ThreadArgs;

void reset_bandwidths()
{
    pthread_mutex_lock(&bandwidth_mutex);
    memset(bandwidths_reduced, 0, sizeof(bandwidths_reduced));
    memset(bandwidths_aug, 0, sizeof(bandwidths_aug));
    pthread_mutex_unlock(&bandwidth_mutex);
}

void *calculate_congestion(void *arg)
{
    while (!stop_congestion_thread)
    {

        pthread_mutex_lock(&bandwidth_mutex);
        if (active_file_index != 0)
        {
            sleep(1);
            pthread_mutex_unlock(&bandwidth_mutex);
            continue;
        }
        double avg_speed_reduced = 0;
        double avg_speed_aug = 0;
        int iter;
        for (iter = 0; iter < aug_index; iter++)
        {
            if (bandwidths_reduced[iter] == 0 && bandwidths_aug[iter] == 0)
            {
                break;
            }
            avg_speed_reduced += bandwidths_reduced[iter];
            avg_speed_aug += bandwidths_aug[iter];
        }
        if (iter > 1 && (avg_speed_aug + avg_speed_reduced == 0))
        {
            pthread_mutex_unlock(&bandwidth_mutex);
            stop_congestion_thread = true;
            continue;
        }
        avg_speed_aug /= iter;
        avg_speed_reduced /= iter;
        double congestion = (1 - ((avg_speed_reduced + avg_speed_aug) / LINK_BANDWIDTH)) * 100;
        congestion -= 10; // 10% overhead
        // Max(0, congestion) to avoid negative congestion
        congestion = (congestion < 0) ? 0 : congestion;
        if (congestion > 20)
        {
            dynamic_progress_threshold = 100 - ((congestion - 20) / 80) * 99.0;
            dynamic_progress_threshold = (dynamic_progress_threshold < max_progress_per_step) ? max_progress_per_step + 1 : dynamic_progress_threshold;
        }
        else
        {
            dynamic_progress_threshold = 100;
        }

        pthread_mutex_unlock(&bandwidth_mutex);
        printf("Congestion: %.2f%%\n", congestion);
        printf("Dynamic Progress Threshold: %.2f%%\n", dynamic_progress_threshold);
        sleep(1);
    }
    pthread_exit(NULL);
    return NULL;
}

bool open_files(char **filenames, int num_files, FILE **files, double *file_sizes)
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

void close_files(FILE **files, int num_files)
{
    for (int i = 0; i < num_files; i++)
    {
        fclose(files[i]);
    }
}

void *connect_socket(int port)
{
    void *sender = zmq_socket(context, ZMQ_PAIR);
    char bind_address[50];
    snprintf(bind_address, sizeof(bind_address), "tcp://%s:%d", CLIENT_IP, port);
    if (zmq_connect(sender, bind_address) != 0)
    {
        fprintf(stderr, "Failed to connect to client: %s\n", zmq_strerror(zmq_errno()));
        return NULL;
    }
    return sender;
}

void close_socket(void *socket)
{
    zmq_close(socket);
}

void send_message(void *socket, char *message, size_t size)
{
    zmq_msg_t msg;
    zmq_msg_init_size(&msg, size);
    zmq_msg_init_data(&msg, message, size, NULL, NULL);
    zmq_msg_send(&msg, socket, 0);
    zmq_msg_close(&msg);
}

void *send_data(void *arg)
{
    // Read args
    ThreadArgs *args = (ThreadArgs *)arg;
    char **filenames = args->filenames;
    int num_files = args->num_files;
    int thread_index = args->thread_index;

    // Connect the socket
    void *sender = connect_socket(BASE_PORT + thread_index);

    // Make steps that will be sent one after one
    zmq_msg_t msg;
    int step = 0;
    while (step < NUM_STEPS)
    {
        pthread_mutex_lock(&bandwidth_mutex);
        max_progress_per_step = 0;
        pthread_mutex_unlock(&bandwidth_mutex);
        // Send all filenames at in consecutive order
        for (int j = 0; j < num_files; j++)
        {
            send_message(sender, filenames[j], strlen(filenames[j]) + 1);
        }
        // End of filenames
        send_message(sender, "", 0);

        // Open file for reading
        FILE *files[num_files];
        bool read_files[num_files];
        double total_files_size[num_files];
        open_files(filenames, num_files, files, total_files_size);

        // Send file data
        size_t chunk_size = 0.01f * total_files_size[0];
        int file_index = 0;
        int iter = 0;
        int num_sent_files = 0;
        double bytes_sent_per_file[num_files];
        for (int i = 0; i < num_files; i++)
        {
            bytes_sent_per_file[i] = 0;
            read_files[i] = false;
        }

        while (num_sent_files < num_files)
        {
            char *buffer = (char *)malloc(chunk_size);
            size_t bytes_read = fread(buffer, 1, chunk_size, files[file_index]);
            bytes_sent_per_file[file_index] += bytes_read;
            fseek(files[file_index], bytes_sent_per_file[file_index], SEEK_SET);
            // Send the file data
            if (bytes_read > 0)
            {
                send_message(sender, buffer, bytes_read);
                // Receive Log info
                zmq_msg_init(&msg);
                zmq_msg_recv(&msg, sender, 0);
                double time_taken = *(double *)zmq_msg_data(&msg);

                // calculate bandwidth based on time taken in Mbps
                double bandwidth = (bytes_read * 8) / (time_taken * 1024 * 1024);
                pthread_mutex_lock(&bandwidth_mutex);
                if (thread_index == 0)
                {
                    bandwidths_reduced[reduced_index] = bandwidth;
                    reduced_index = (reduced_index + 1) % BANDWIDTH_SIZE;
                }
                else
                {
                    bandwidths_aug[aug_index] = bandwidth;
                    aug_index = (aug_index + 1) % BANDWIDTH_SIZE;
                }
                pthread_mutex_unlock(&bandwidth_mutex);

                // Check if the file has been completely sent based on progress
                if (thread_index == 1)
                {
                    size_t progress = (bytes_sent_per_file[file_index] / total_files_size[file_index]) * 100.0;
                    printf("File: %s, Progress: %ld%%\n", filenames[file_index], progress);
                    pthread_mutex_lock(&bandwidth_mutex);
                    if (progress >= dynamic_progress_threshold)
                    {
                        // Send the close message with 0 bytes
                        send_message(sender, "", 0);
                        read_files[file_index] = true;
                        num_sent_files++;
                    }
                    max_progress_per_step = (file_index == 0) ? progress : max_progress_per_step;
                    pthread_mutex_unlock(&bandwidth_mutex);
                }
                iter++;
                if ((read_files[file_index]) || (iter % 10 == 0))
                {
                    iter = 0;
                    file_index = (file_index + 1) % num_files;
                    pthread_mutex_lock(&bandwidth_mutex);
                    active_file_index = file_index;
                    pthread_mutex_unlock(&bandwidth_mutex);
                }
                free(buffer);
            }
            // Check if the file has been completely sent for R
            else
            {
                // Send the close message with 0 bytes
                send_message(sender, "", 0);
                pthread_mutex_lock(&bandwidth_mutex);
                pthread_mutex_unlock(&bandwidth_mutex);
                // Close the file
                close_files(files, num_files);
                break;
            }
        }

        // Alert message
        // if 0 that means the port is complete and no more steps,
        // if 1 move to the next step by incrementing step
        reset_bandwidths();
        bool is_port_complete = (step == NUM_STEPS - 1);
        send_message(sender, (is_port_complete) ? "0" : "1", 1);
        zmq_msg_init(&msg);
        zmq_msg_recv(&msg, sender, 0);
        printf("Received ack message: %s\n", (char *)zmq_msg_data(&msg));

        // Increment step
        step++;
    }

    zmq_msg_close(&msg);
    close_socket(sender);
    pthread_exit(NULL);
    return NULL;
}

int main()
{
    printf("Starting Sender...\n");
    context = zmq_ctx_new();
    pthread_t reduced_thread, aug_thread, congestion_thread;

    pthread_create(&congestion_thread, NULL, calculate_congestion, NULL);

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
