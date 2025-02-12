#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zmq.h>
#include <pthread.h>
#include <unistd.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#define CHUNK_SIZE 1024
#define BASE_PORT 4444
#define CLIENT_IP "129.114.108.224"

typedef struct {
    const char *filename;
    int thread_index;
} ThreadArgs;


void *context;

void get_interface_for_address(const char *dest_ip, char *interface, size_t len) {
    char command[256];
    snprintf(command, sizeof(command), "ip route get %s", dest_ip);

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

void* send_file(void *arg){
    ThreadArgs *args = (ThreadArgs *)arg;
    const char *filename = args->filename;
    int thread_index = args->thread_index;

    const char *directory = "./data/";
    char filepath[256];
    sprintf(filepath, "%s%s", directory, filename);
    
    void *requester = zmq_socket(context, ZMQ_REQ);
    int port = BASE_PORT + 1 + thread_index; 
    char bind_address[50];
    printf("Connecting to client on port %d\n", port);
    snprintf(bind_address, sizeof(bind_address), "tcp://%s:%d", CLIENT_IP, port);
    if(zmq_connect(requester, bind_address) != 0){
        fprintf(stderr, "Failed to connect to client: %s\n", zmq_strerror(zmq_errno()));
        return NULL;
    }

    FILE *file = fopen(filepath, "rb");
    if(file == NULL){
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
    char *buffer = (char*) malloc(CHUNK_SIZE);
    if(buffer == NULL){
        fprintf(stderr, "Failed to allocate memory\n");
        fclose(file);
        return NULL;
    } 
    size_t bytes_read;
    int i=0;
    while((bytes_read = fread(buffer, 1, CHUNK_SIZE, file)) > 0){
        if(bytes_read <= 0){
            printf("Failed to read from file %s\n", filename);
            break;
        }
        //printf("Sending chunk %d from file %s\n", i, filename);
        zmq_msg_init_size(&msg, bytes_read);
        zmq_msg_init_data(&msg, buffer, bytes_read, NULL, NULL);
        zmq_msg_send(&msg, requester, 0);

        zmq_msg_init(&msg);
        zmq_msg_recv(&msg, requester, 0);
        //printf("Received acknowledgment: %s for file %s\n", (char*) zmq_msg_data(&msg), filename);
        if(strncmp((char *) zmq_msg_data(&msg), "PAUSE", 5) == 0){
            	printf("Client is busy\n");
	    	//Construct the args of the command
		const char *group_name = (thread_index == 0 ? "high_prio" : "low_prio");
        	char pid[20];
		snprintf(pid, sizeof(pid), "%d", getpid());
        	char interface[50];
        	get_interface_for_address("129.114.108.224", interface, sizeof(interface));
    		const char *priority = thread_index == 0 ? "10" : "1";
		const char *script_path = "/home/cc/zmq/scripts/controlNetPrio.sh";
		// Construct the full command to run with arguments
        	char command[256];
        	printf("sudo %s %s %s %s %s\n", script_path, group_name, pid, interface, priority);
		snprintf(command, sizeof(command), "sudo %s %s %s %s %s", script_path, group_name, pid, interface, priority);
        	// Run the script with arguments
        	int status = system(command);
		printf("Script status: %d\n", status);
        };
        i++;
    }
    //send the close message with 0 bytes
    zmq_msg_init_size(&msg, bytes_read);
    zmq_msg_send(&msg, requester, 0);
    //recf ack from client that he has received all the data
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
    if(zmq_connect(requester, bind_address) != 0){
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

int main(){
    context = zmq_ctx_new();
    const char *filenames[] = {"delta_r_xgc_o.bin", "delta_z_xgc_o.bin", "delta_xgc_o.bin", "reduced_data_xgc_16.bin"};
    const int num_threads = sizeof(filenames) / sizeof(filenames[0]);
    printf("Number of threads: %d\n", num_threads);
    pthread_t threads[num_threads];

    send_no_files(num_threads);
    for(int i = 0; i < num_threads; i++){
        ThreadArgs *args = malloc(sizeof(ThreadArgs));
        if (args == NULL) {
            perror("Failed to allocate memory for thread arguments");
            return -1;
        }
        args->filename = filenames[i];
        args->thread_index = i;
        if(pthread_create(&threads[i], NULL, send_file, (void *)args) != 0){
            perror("Error creating thread");
            return -1;
        }
    }

    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    printf("Sending files completed\n");
    zmq_ctx_destroy(context);
    return 0;
}
