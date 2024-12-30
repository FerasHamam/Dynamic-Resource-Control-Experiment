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

#define BASE_PORT 4445
#define CLIENT_IP "129.114.108.224"
#define NUM_STEPS 3

// TODO
volatile bool stop_congestion_thread = false;

void *context;
typedef struct
{
    char **filenames;
    int num_files;
    int thread_index;
} ThreadArgs;


// TODO
// Function to get the network interface for the given IP
void get_interface_for_address(char *interface, size_t len)
{
    char command[256];
    snprintf(command, sizeof(command), "ip route get %s", CLIENT_IP);

    // Open a pipe to read the output of the command
    FILE *fp = popen(command, "r");
    if (fp == NULL)
    {
        perror("popen");
        snprintf(interface, len, "unknown");
        return;
    }

    // Read the output from the `ip route get` command
    char buffer[512];
    while (fgets(buffer, sizeof(buffer), fp) != NULL)
    {
        // Try to find the interface in the output
        if (strstr(buffer, "dev") != NULL)
        {
            // Get the interface name (after "dev")
            char *dev = strstr(buffer, "dev");
            if (dev)
            {
                dev += 4; // Skip past "dev "
                char *end = strchr(dev, ' ');
                if (end)
                {
                    *end = '\0'; // Null-terminate the interface name
                    snprintf(interface, len, "%s", dev);
                    break;
                }
            }
        }
    }
    // Close the pipe
    fclose(fp);
}

// TODO
// Function to remove traffic control rules
void remove_rules()
{
    char interface[50];
    get_interface_for_address(interface, sizeof(interface));
    char command[256];
    snprintf(command, sizeof(command), "sudo ../scripts/removeRules.sh %s &", interface);
    system(command);
}

// Function to adjust socket priority
void adjust_socket_shaping()
{
    int port = BASE_PORT;
    char interface[50];
    get_interface_for_address(interface, sizeof(interface));
    char command[256];
    snprintf(command, sizeof(command), "sudo ../scripts/controlNetPrio.sh %s %s %d %d &", interface, CLIENT_IP, port + 1, 4);
    if (system(command) != 0)
    {
        fprintf(stderr, "Error: Failed to execute command: %s\n", command);
    }
}

// TODO
// This function simulates congestion by periodically adjusting socket priority and removing rules
void *congestion_control(void *arg)
{
    // Timer-related variables
    time_t last_adjust_time = time(NULL);
    const int congestion_interval = 5; // Trigger every 5 seconds

    bool is_shaped = false; // Set to true if traffic shaping is enabled

    while (!stop_congestion_thread)
    {
        time_t current_time = time(NULL);
        // Check if 5 seconds have passed since the last adjustment
        if (difftime(current_time, last_adjust_time) >= congestion_interval)
        {
            if (is_shaped)
                continue;
            printf("Simulating congestion... Adjusting socket priority and removing rules\n");
            is_shaped ? remove_rules() : adjust_socket_shaping();
            is_shaped = !is_shaped;
            last_adjust_time = current_time; // Update last adjust time
        }
        // Sleep for a short period to prevent high CPU usage
        usleep(100000); // Sleep for 100 ms
    }
    return NULL;
}

void *send_data(void *arg)
{
    // Read args
    ThreadArgs *args = (ThreadArgs *)arg;
    char **filenames = args->filenames;
    int num_files = args->num_files;
    int thread_index = args->thread_index;

    // Initialize the socket
    void *sender = zmq_socket(context, ZMQ_PAIR);
    int port = BASE_PORT + thread_index;
    char bind_address[50];
    printf("Connecting to Receiver on port %d\n", port);
    snprintf(bind_address, sizeof(bind_address), "tcp://%s:%d", CLIENT_IP, port);
    if (zmq_connect(sender, bind_address) != 0)
    {
        fprintf(stderr, "Failed to connect to client: %s\n", zmq_strerror(zmq_errno()));
        return NULL;
    }

    // Make steps of 5 that will be sent one after one
    int step = 0;
    zmq_msg_t msg;
    while (step < NUM_STEPS)
    {
        // Send file
        const char *directory = "../data/";

        for (int i = 0; i < num_files; i++)
        {
            // Send file name
            zmq_msg_init_size(&msg, strlen(filenames[i]));
            zmq_msg_init_data(&msg, filenames[i], strlen(filenames[i]), NULL, NULL);
            zmq_msg_send(&msg, sender, 0);

            // Open file for reading
            char filepath[256];
            snprintf(filepath, sizeof(filepath), "%s%s", directory, filenames[i]);
            FILE *file = fopen(filepath, "rb");
            if (file == NULL)
            {
                fprintf(stderr, "Failed to open file %s\n", filenames[i]);
                zmq_close(sender);
                return NULL;
            }

            // Send file data of 1mb chunks
            size_t bytes_read;
            size_t chunk_size = 1024 * 1024;
            char *buffer = (char *)malloc(chunk_size);
            if (buffer == NULL)
            {
                fprintf(stderr, "Failed to allocate memory\n");
                fclose(file);
                zmq_close(sender);
                return NULL;
            }
            while ((bytes_read = fread(buffer, 1, chunk_size, file)) > 0)
            {
                if (bytes_read <= 0)
                {
                    printf("Failed to read from file %s\n", filenames[i]);
                    break;
                }

                zmq_msg_init_size(&msg, bytes_read);
                zmq_msg_init_data(&msg, buffer, bytes_read, NULL, NULL);
                zmq_msg_send(&msg, sender, 0);
            }

            // Send the close message with 0 bytes
            zmq_msg_init_size(&msg, 0);
            zmq_msg_send(&msg, sender, 0);

            // Alert message
            // if 0 that means the port is complete and no more steps,
            // if 1 more files to come with the same step,
            // if 2 move to the next step by incrementing step
            char *message = (step == NUM_STEPS-1 && i == num_files-1) ? "0" : (i < num_files - 1) ? "1" : "2";
            //printf("Filename: %s, Port %d, step: %d, Sentfiles: %d out of %d, Sending alert message: %s \n", filenames[i], port, step+1, i+1, num_files, message);
            zmq_msg_init_size(&msg, strlen(message));
            zmq_msg_init_data(&msg, message, strlen(message), NULL, NULL);
            zmq_msg_send(&msg, sender, 0);

            // Ack message
            if(*message == '1')
            {
                continue;
            }
            zmq_msg_init(&msg);
            zmq_msg_recv(&msg, sender, 0);
            printf("Received ack message: %s\n", (char *)zmq_msg_data(&msg));
        }
        // Increment step
        step++;
    }

    zmq_msg_close(&msg);
    zmq_close(sender);
    pthread_exit(NULL);
    return NULL;
}

int main()
{   
    //TODO
    //remove_rules();

    printf("Starting Sender...\n");
    context = zmq_ctx_new();
    pthread_t high_quality_thread, low_quality_thread;

    // Filenames to be sent
    char *HQfilenames[] = {"reduced_data_xgc_16.bin"};
    char *LQfilenames[] = {"delta_r_xgc_o.bin", "delta_z_xgc_o.bin", "delta_xgc_o.bin"};

    // Create High Quality thread arguments
    ThreadArgs HQargs;
    HQargs.filenames = HQfilenames;
    HQargs.num_files = 1;
    HQargs.thread_index = 0;

    // Create a thread for sending files
    pthread_t send_thread_hq;
    if (pthread_create(&send_thread_hq, NULL, send_data, &HQargs) != 0)
    {
        fprintf(stderr, "Error: Failed to create send file thread\n");
        return EXIT_FAILURE;
    }

    // Create Low Quality thread arguments
    ThreadArgs LQargs;
    LQargs.filenames = LQfilenames;
    LQargs.num_files = 3;
    LQargs.thread_index = 1;

    // Create a thread for sending files
    pthread_t send_thread_lq;
    if (pthread_create(&send_thread_lq, NULL, send_data, &LQargs) != 0)
    {
        fprintf(stderr, "Error: Failed to create send file thread\n");
        return EXIT_FAILURE;
    }

    // TODO
    // Create a thread for congestion control
    // pthread_t congestion_thread;
    // if (pthread_create(&congestion_thread, NULL, congestion_control, NULL) != 0)
    // {
    //     fprintf(stderr, "Error: Failed to create congestion control thread\n");
    //     return EXIT_FAILURE;
    // }

    // Wait for the send file thread to finish
    pthread_join(send_thread_hq, NULL);
    pthread_join(send_thread_lq, NULL);

    // TODO
    // // Stop the congestion control thread
    // stop_congestion_thread = true;
    // pthread_join(congestion_thread, NULL);

    // Clean up ZeroMQ context
    zmq_ctx_destroy(context);

    return EXIT_SUCCESS;
}
