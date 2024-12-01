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

#define HIGH_PRIORITY_CHUNK_SIZE 12500000
#define LOW_PRIORITY_CHUNK_SIZE 1250000
#define BASE_PORT 4444
#define CLIENT_IP "129.114.108.224"

volatile bool stop_congestion_thread = false;

typedef struct {
    const char *filename;
    int thread_index;
} ThreadArgs;

void *context;

// Function to get the network interface for the given IP
void get_interface_for_address(char *interface, size_t len) {
    char command[256];
    snprintf(command, sizeof(command), "ip route get %s", CLIENT_IP);

    // Open a pipe to read the output of the command
    FILE *fp = popen(command, "r");
    if (fp == NULL) {
        perror("popen");
        snprintf(interface, len, "unknown");
        return;
    }

    // Read the output from the `ip route get` command
    char buffer[512];
    while (fgets(buffer, sizeof(buffer), fp) != NULL) {
        // Try to find the interface in the output
        if (strstr(buffer, "dev") != NULL) {
            // Get the interface name (after "dev")
            char *dev = strstr(buffer, "dev");
            if (dev) {
                dev += 4; // Skip past "dev "
                char *end = strchr(dev, ' ');
                if (end) {
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

// Function to remove traffic control rules
void remove_rules() {
    char interface[50];
    get_interface_for_address(interface, sizeof(interface));
    char command[256];
    snprintf(command, sizeof(command), "sudo ../scripts/removeRules.sh %s &", interface);
    system(command);
}

// Function to adjust socket priority
void adjust_socket_shaping() {
    int port = BASE_PORT;
    char interface[50];
    get_interface_for_address(interface, sizeof(interface));
    char command[256];
    snprintf(command, sizeof(command), "sudo ../scripts/controlNetPrio.sh %s %s %d %d &", interface, CLIENT_IP, port+1, 4);
    if (system(command) != 0) {
        fprintf(stderr, "Error: Failed to execute command: %s\n", command);
    }
}

// This function simulates congestion by periodically adjusting socket priority and removing rules
void* congestion_control(void *arg) {
    // Timer-related variables
    time_t last_adjust_time = time(NULL);
    const int congestion_interval = 5;  // Trigger every 5 seconds

    bool is_shaped = false; // Set to true if traffic shaping is enabled
    
    while (!stop_congestion_thread) {
        time_t current_time = time(NULL);
        // Check if 5 seconds have passed since the last adjustment
        if (difftime(current_time, last_adjust_time) >= congestion_interval) {
            if(is_shaped)
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

void* send_file(void *arg) {
    ThreadArgs *args = (ThreadArgs *)arg;
    const char *filename = args->filename;
    int thread_index = args->thread_index;

    const char *directory = "../data/";
    char filepath[256];
    sprintf(filepath, "%s%s", directory, filename);
    
    void *requester = zmq_socket(context, ZMQ_REQ);
    int port = BASE_PORT + 1 + thread_index; 
    char bind_address[50];
    printf("Connecting to client on port %d\n", port);
    snprintf(bind_address, sizeof(bind_address), "tcp://%s:%d", CLIENT_IP, port);
    if (zmq_connect(requester, bind_address) != 0) {
        fprintf(stderr, "Failed to connect to client: %s\n", zmq_strerror(zmq_errno()));
        return NULL;
    }

    FILE *file = fopen(filepath, "rb");
    if (file == NULL) {
        fprintf(stderr, "Failed to open file %s\n", filename);
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    printf("Sending file %s with size: %ld\n", filename, file_size);

    // Send filename
    zmq_msg_t msg;
    zmq_msg_init(&msg);
    zmq_msg_init_data(&msg, (void *)filename, strlen(filename) + 1, NULL, NULL);
    zmq_msg_send(&msg, requester, 0);

    // Send file content
    size_t bytes_read;
    size_t chunk_size = thread_index == 0 ? HIGH_PRIORITY_CHUNK_SIZE : LOW_PRIORITY_CHUNK_SIZE;
    char *buffer = (char*) malloc(chunk_size);
    if (buffer == NULL) {
        fprintf(stderr, "Failed to allocate memory\n");
        fclose(file);
        return NULL;
    }
    while ((bytes_read = fread(buffer, 1, chunk_size, file)) > 0) {
        if (bytes_read <= 0) {
            printf("Failed to read from file %s\n", filename);
            break;
        }

        zmq_msg_init_size(&msg, bytes_read);
        zmq_msg_init_data(&msg, buffer, bytes_read, NULL, NULL);
        zmq_msg_send(&msg, requester, 0);

        zmq_msg_init(&msg);
        zmq_msg_recv(&msg, requester, 0);
    }

    // Send the close message with 0 bytes
    zmq_msg_init_size(&msg, 0);
    zmq_msg_send(&msg, requester, 0);

    // Receive acknowledgment from client
    zmq_msg_init(&msg);
    zmq_msg_recv(&msg, requester, 0);

    fclose(file);
    free(buffer);
    zmq_close(requester);
    zmq_msg_close(&msg);
    pthread_exit(NULL);
    return NULL;
}

void* send_no_files(int value) { 
    char bind_address[50];
    snprintf(bind_address, sizeof(bind_address), "tcp://129.114.108.224:%d", BASE_PORT);
    void *requester = zmq_socket(context, ZMQ_REQ);
    if (zmq_connect(requester, bind_address) != 0) {
        fprintf(stderr, "Failed to connect to server: %s\n", zmq_strerror(zmq_errno()));
        return NULL;
    }
    zmq_msg_t msg;
    int *data = (int *) malloc(sizeof(int));
    *data = value;
    zmq_msg_init_data(&msg, data, sizeof(int), NULL, NULL);
    zmq_msg_send(&msg, requester, 0);

    zmq_msg_init(&msg);
    zmq_msg_recv(&msg, requester, 0);
    printf("Received acknowledgment for number of files: %s\n", (char*) zmq_msg_data(&msg));
    zmq_msg_close(&msg);
    free(data);
    zmq_close(requester);
    return NULL;
}

int main() {
    context = zmq_ctx_new();
    remove_rules();
    const char *filenames[] = {"reduced_data_xgc_16.bin", "delta_r_xgc_o.bin", "delta_z_xgc_o.bin", "delta_xgc_o.bin"};
    const int num_threads = sizeof(filenames) / sizeof(filenames[0]);
    printf("Number of threads: %d\n", num_threads);
    pthread_t threads[num_threads];
    
    // Start congestion control thread
    pthread_t congestion_thread;
    if (pthread_create(&congestion_thread, NULL, congestion_control, NULL) != 0) {
        perror("Error creating congestion control thread");
        return -1;
    }
    send_no_files(num_threads);
    for (int i = 0; i < num_threads; i++) {
        ThreadArgs *args = malloc(sizeof(ThreadArgs));
        if (args == NULL) {
            perror("Failed to allocate memory for thread arguments");
            return -1;
        }
        args->filename = filenames[i];
        args->thread_index = i;
        if (pthread_create(&threads[i], NULL, send_file, (void *)args) != 0) {
            perror("Error creating thread");
            return -1;
        }
    }

    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    printf("Sending files completed\n");

    stop_congestion_thread = true;
    pthread_join(congestion_thread, NULL);

    zmq_ctx_destroy(context);
    return 0;
}
