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
#define NUM_STEPS 20
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
int aug_size_prediciton_based_on_congestion[NUM_STEPS];
int step_aug = 0;
volatile bool stop_congestion_thread = false;
void *context;

void calculate_congestion(void *arg)
{
    int last_processed_step = 0;
    while (!stop_congestion_thread)
    {
        if (step_aug <= last_processed_step || (step_aug % 3 != 0))
        {
            sleep(5);
            continue;
        }

        FILE *prediciton_file = fopen("prediction.txt", "r");
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
                sleep(2);
                prediciton_file = fopen("prediction.txt", "r");
            }
        }

        double congestion;
        // double throughput;
        fscanf(prediciton_file, "%lf", &congestion);
        // fscanf(prediciton_file, "%lf", &throughput);
        fclose(prediciton_file);
        remove("prediction.txt");

        int dynamic_progress_threshold = 100;
        printf("Congestion: %.2f%%\n", congestion);
        if (congestion > 20.0)
        {
            dynamic_progress_threshold = 100 - (congestion - 20);
        }

        pthread_mutex_lock(&bandwidth_mutex);
        for (int i = step_aug; i < step_aug + 5; i++)
        {
            if (i < NUM_STEPS)
            {
                aug_size_prediciton_based_on_congestion[i] = dynamic_progress_threshold;
                continue;
            }
            break;
        }
        last_processed_step = step_aug + 1;
        pthread_mutex_unlock(&bandwidth_mutex);
    }
    printf("Exiting congestion thread\n");
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

void store_bandwidth_and_time(double time_taken, double bytes)
{
    pid_t pid = fork();

    if (pid == -1)
    {
        perror("fork failed");
        return;
    }

    if (pid == 0)
    {
        FILE *log_file = fopen("log.txt", "a");
        if (log_file == NULL)
        {
            perror("fopen failed");
            exit(1); // Exit child process if file cannot be opened
        }
        fprintf(log_file, "%f:%f\n", time_taken, bytes);
        fclose(log_file);
        exit(0); // Child process exits after writing
    }
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
        double dynamic_progress = 100;
        int iter = 0;
        while (num_sent_files < num_files)
        {
            if (thread_index == 1)
            {
                pthread_mutex_lock(&bandwidth_mutex);
                dynamic_progress = aug_size_prediciton_based_on_congestion[step];
                pthread_mutex_unlock(&bandwidth_mutex);
            }
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
                    store_bandwidth_and_time(log_message, bytes_read);
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
