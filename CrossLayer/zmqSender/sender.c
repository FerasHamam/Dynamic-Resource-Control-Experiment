#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zmq.h>
#include <json-c/json.h>
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
#define BANDWIDTH_SIZE 100
#define LINK_BANDWIDTH 200.0 * 1000.0 * 1000.0

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

// Global shared resources
pthread_mutex_t bandwidth_mutex = PTHREAD_MUTEX_INITIALIZER;
double time_taken[BANDWIDTH_SIZE][2] = {0};
double bytes_sent[BANDWIDTH_SIZE][2] = {0};
int reduced_index = 0;
int aug_index = 0;
volatile double dynamic_progress_threshold = 100.0;
volatile double max_progress_per_step = 0;
volatile bool stop_congestion_thread = false;
volatile int active_file_index = 0;
volatile double curr_reduced_file_size[NUM_STEPS] = {0};
volatile double curr_aug_files_size[NUM_STEPS] = {0};
volatile double aug_file_size = 0;
volatile int step_aug = 0;
volatile int step_reduced = 0;
void *context;
typedef struct
{
    char **filenames;
    int num_files;
    int thread_index;
} ThreadArgs;

void start_net_layer()
{
    pid_t pid = fork();
    if (pid < 0)
    {
        perror("Failed to fork for Python script");
        exit(EXIT_FAILURE);
    }
    else if (pid == 0)
    {
        const char *script_path = "../scripts/NetLayer.py";
        execlp("python3", "python3", script_path, "--dest_ip", CLIENT_IP, "--ports", "4444", "4445", NULL);
        perror("Failed to execute Python script");
        exit(EXIT_FAILURE);
    }
    else
    {
        printf("Python script started with PID: %d\n", pid);
    }
}

void write_json(const char *file_path, int reduced_size, int aug_size, double link_bandwidth, double congestion)
{
    // Create a JSON object
    json_object *root_obj = json_object_new_object();

    // Add to JSON object
    json_object *file_sizes = json_object_new_array();
    json_object_array_add(file_sizes, json_object_new_int(reduced_size));
    json_object_array_add(file_sizes, json_object_new_int(aug_size));
    json_object_object_add(root_obj, "file_sizes", file_sizes);
    json_object_object_add(root_obj, "link_bandwidth", json_object_new_int(link_bandwidth));
    json_object_object_add(root_obj, "congestion", json_object_new_int(congestion));

    // Write JSON object to file
    FILE *file = fopen(file_path, "w");
    if (file == NULL)
    {
        perror("Error opening file");
        json_object_put(root_obj); // Free the JSON object
        return;
    }
    
    fprintf(file, "%s", json_object_to_json_string_ext(root_obj, JSON_C_TO_STRING_PRETTY));
    fclose(file);
    json_object_put(root_obj);
}

void *calculate_congestion(void *arg)
{

    while (!stop_congestion_thread)
    {
        pthread_mutex_lock(&bandwidth_mutex);
        while(active_file_index != 0)
        {
            pthread_mutex_unlock(&bandwidth_mutex);
            usleep(100000);
            continue;
        }
        double avg_speed_reduced = 0;
        double avg_speed_aug = 0;
        int iter;
        for (iter = 0; iter < BANDWIDTH_SIZE; iter++)
        {
            // Check if the times are not 0 (Divide by Zero) and accumulate the values
            if (bytes_sent[iter][0] == 0 && bytes_sent[iter][1] == 0)
            {
                break;
            }
            avg_speed_reduced += time_taken[iter][0] > 0 ? ((bytes_sent[iter][0] * 8) / time_taken[iter][0]) : 0;
            avg_speed_aug += time_taken[iter][1] > 0 ? ((bytes_sent[iter][1] * 8) / time_taken[iter][1]) : 0;
        }
        if (iter == 0)
        {
            pthread_mutex_unlock(&bandwidth_mutex);
            usleep(250000);
            continue;
        }
        avg_speed_aug /= iter;
        avg_speed_reduced /= iter;
        double total_bandwidth = avg_speed_reduced + avg_speed_aug;
        double congestion = (int)((1.0 - (total_bandwidth / (LINK_BANDWIDTH))) * 100);
        if (congestion < 10)
        {
            dynamic_progress_threshold = 100.0;
        }
        else
        {
            dynamic_progress_threshold = 100.0 - (congestion - 10.0);
            if (dynamic_progress_threshold < max_progress_per_step)
                dynamic_progress_threshold = max_progress_per_step + 2;
        }
        // Reset the values for the next acting
        int min_step = 0;
        if (step_reduced < step_aug)
        {
            min_step = step_reduced;
        }
        else
        {
            min_step = step_aug;
        }

        double partial_aug_file_size = 0;
        if (min_step == step_aug)
        {
            partial_aug_file_size = (dynamic_progress_threshold * aug_file_size / 100.0) - (aug_file_size - curr_aug_files_size[min_step]);
        }
        write_json("../scripts/congestion.json", curr_reduced_file_size[min_step], partial_aug_file_size, LINK_BANDWIDTH, congestion);
        printf("speed_reduced: %.2f, speed_aug: %.2f, congestion: %f%%\n", avg_speed_reduced, avg_speed_aug, congestion);
         printf("Dynamic Progress Threshold: %.2f%%\n", dynamic_progress_threshold);
        pthread_mutex_unlock(&bandwidth_mutex);
        usleep(250000);
    }
    pthread_exit(NULL);
    return NULL;
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
        pthread_mutex_lock(&bandwidth_mutex);
        if (files_type == REDUCED)
        {
            curr_reduced_file_size[step_reduced] += file_sizes[i];
        }
        else
        {
            curr_aug_files_size[step_aug] += file_sizes[i];
        }
        aug_file_size = curr_aug_files_size[step_aug];
        pthread_mutex_unlock(&bandwidth_mutex);
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

    void *sender = connect_socket(BASE_PORT + thread_index);
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
        size_t chunk_size = 0.01f * total_files_size[0];
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
            pthread_mutex_lock(&bandwidth_mutex);
            if (thread_index == 0)
            {
                curr_reduced_file_size[step_reduced] -= bytes_read;
            }
            else
            {
                curr_aug_files_size[step_aug] -= bytes_read;
            }
            pthread_mutex_unlock(&bandwidth_mutex);
            // Send the file data
            if (bytes_read > 0)
            {
                send_data_chunk(sender, buffer, bytes_read);
                // Receive Log info
                double log_message;
                recv_double_data_chunk(sender, &log_message);

                // calculate bandwidth based on time taken in Mbps
                pthread_mutex_lock(&bandwidth_mutex);
                time_taken[thread_index ? aug_index : reduced_index][thread_index] = log_message;
                bytes_sent[thread_index ? aug_index : reduced_index][thread_index] = bytes_read;
                if (thread_index == 0)
                {
                    reduced_index = (reduced_index + 1) % BANDWIDTH_SIZE;
                }
                else
                {
                    aug_index = (aug_index + 1) % BANDWIDTH_SIZE;
                }
                pthread_mutex_unlock(&bandwidth_mutex);

                // Check if the file has been completely sent based on progress
                if (thread_index == 1)
                {
                    double progress = ((double)bytes_sent_per_file[file_index] / (double)total_files_size[file_index]) * 100.0f;
                    // printf("File: %s, Progress: %.2f%%\n", filenames[file_index], progress);
                    double dynamic_progress = 0.0;
                    pthread_mutex_lock(&bandwidth_mutex);
                    dynamic_progress = dynamic_progress_threshold;
                    pthread_mutex_unlock(&bandwidth_mutex);
                    if (progress >= dynamic_progress)
                    {
                        // Send the close message with 0 bytes
                        printf("File: %s, Progress: %.2f%%\n", filenames[file_index], progress);
                        send_data_chunk(sender, "", 0);
                        read_files[file_index] = true;
                        num_sent_files++;
                    }
                    pthread_mutex_lock(&bandwidth_mutex);
                    max_progress_per_step = (progress < max_progress_per_step) ? progress : max_progress_per_step;
                    pthread_mutex_unlock(&bandwidth_mutex);
                }
                iter++;
                if ((read_files[file_index]) || (iter % 1 == 0))
                {
                    file_index = (file_index + 1) % num_files;
                    pthread_mutex_lock(&bandwidth_mutex);
                    active_file_index = file_index;
                    pthread_mutex_unlock(&bandwidth_mutex);
                    iter = 0;
                }
                free(buffer);
            }
            else
            {
                // Send the close message with 0 bytes
                send_data_chunk(sender, "", 0);
                close_files(files, num_files, thread_index ? AUG : REDUCED);
                break;
            }
        }

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
        pthread_mutex_lock(&bandwidth_mutex);
        if (thread_index == 0)
        {
            step_reduced = step;
        }
        else
        {
            step_aug = step;
        }
        pthread_mutex_unlock(&bandwidth_mutex);
    }

    close_socket(sender);
    pthread_exit(NULL);
    return NULL;
}

int main()
{
    // start_net_layer();
    // sleep(5);
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

    // stop_congestion_thread = true;
    pthread_join(congestion_thread, NULL);
    // Clean up ZeroMQ context
    zmq_ctx_destroy(context);

    return EXIT_SUCCESS;
}
