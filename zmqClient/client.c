#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zmq.h>
#include <unistd.h>
#include <stdbool.h>
#include <pthread.h>
#include <stdatomic.h>

void *context;

char* construct_filepath(const char* filename) {
    char* filepath = (char*) malloc(256 * sizeof(char));
    char new_directory[256];
    char directory[128] = "/home/cc/zmq/data";
    
    if (strstr(filename, "reduced") != NULL) {
        snprintf(new_directory, sizeof(new_directory), "%s/%s/", directory, "reduced");
    } else if (strstr(filename, "delta") != NULL) {
        snprintf(new_directory, sizeof(new_directory), "%s/%s/", directory, "delta");
    } else {
        snprintf(new_directory, sizeof(new_directory), "%s/", directory);
    }
    
    sprintf(filepath, "%s%s", new_directory, filename);
    return filepath;
}

int recv_no_files() {
    void *responder = zmq_socket(context, ZMQ_REP);
    if (zmq_bind(responder, "tcp://0.0.0.0:4444") != 0) {
        fprintf(stderr, "Failed to bind socket: %s\n", zmq_strerror(zmq_errno()));
        zmq_close(responder);  // Close the socket if binding fails
        return -1; // Return an error code
    }  
    printf("Waiting to receive the number of files...\n");
    zmq_msg_t msg;
    zmq_msg_init(&msg);
    if (zmq_msg_recv(&msg, responder, 0) == -1) {
        fprintf(stderr, "Failed to receive message: %s\n", zmq_strerror(zmq_errno()));
        zmq_msg_close(&msg);
        zmq_msg_close(&msg);
        return -1;
    }
    int value;
    memcpy(&value, zmq_msg_data(&msg), sizeof(int));
    zmq_msg_init_size(&msg, strlen("ACK files!") + 1);
    memcpy(zmq_msg_data(&msg), "ACK files!", strlen("ACK files!") + 1);
    zmq_msg_send(&msg, responder, 0);
    zmq_close(responder);  // Close the responder socket
    zmq_msg_close(&msg);   // Close the message to release resources
    return value; // Return the received value
}

void* recv_file(void *arg){
    int thread_index = *(int *)arg;  // Retrieve thread index
    free(arg);
    void *responder = zmq_socket(context, ZMQ_REP);
    int port = 4445 + thread_index; 
    char bind_address[50];
    snprintf(bind_address, sizeof(bind_address), "tcp://0.0.0.0:%d", port);
    if(zmq_bind(responder, bind_address) != 0){
        fprintf(stderr, "Failed to bind socket: %s\n", zmq_strerror(zmq_errno()));
        return NULL;
    }    
    printf("Binding to server on port %d\n", port);

    zmq_msg_t msg;
    zmq_msg_init(&msg);
    zmq_msg_recv(&msg, responder, 0);
    char *data = zmq_msg_data(&msg);
    int length = zmq_msg_size(&msg);
    char *filename = (char *) malloc(length + 1);
    strcpy(filename, data);
    printf("Received filename: %s\n", filename);    
    char* filepath = construct_filepath(filename);

    char ack[256];
    snprintf(ack, sizeof(ack), "Received Path: %s", filepath);
    zmq_msg_init_size(&msg, strlen(ack) + 1);
    memcpy(zmq_msg_data(&msg), ack, strlen(ack) + 1);
    zmq_msg_send(&msg, responder, 0);
    sleep(2);
    int chunk_iteration = 0;
    bool enable = true;
    FILE *file = fopen(filepath, "ab");
    if(file == NULL){
        fprintf(stderr, "Failed to open file %s\n", filename);
            free(filename);
            return NULL;
    }
    char ackMsg[256];
    int ackMsgSize = sizeof(ackMsg);
    int modVal = 50;  
    while(enable)
    {   
   	zmq_msg_init(&msg);
        zmq_msg_recv(&msg, responder, 0);
        long chunk_size = zmq_msg_size(&msg);
        char *buffer = (char *) malloc(chunk_size);
        char *data = zmq_msg_data(&msg);
        //exit when msg is of size 0
        if(chunk_size == 0 || buffer == NULL){
            enable = false;
            continue;
        }
        memcpy(buffer, zmq_msg_data(&msg), chunk_size);
        fwrite(buffer, 1, chunk_size, file);
        free(buffer);
        sprintf(ackMsg, "Chunck %d from file %s: Received Successfully!", chunk_iteration, filename);
	//printf("%s\n", ackMsg);  
        zmq_msg_init_size(&msg, ackMsgSize + 1);
        memcpy(zmq_msg_data(&msg), ackMsg, ackMsgSize + 1);
        zmq_msg_init_data(&msg, ackMsg, ackMsgSize, NULL, NULL);
        zmq_msg_send(&msg, responder, 0);
        chunk_iteration++;
        if(!enable)
            break;
    }
    //send the ack for close message with 0 bytes
    sprintf(ackMsg, "File %s received successfully!", filename);    
    printf("%s\n", ackMsg);
    zmq_msg_init_size(&msg, ackMsgSize + 1);
    zmq_msg_init_data(&msg, ackMsg, ackMsgSize, NULL, NULL);
    zmq_msg_send(&msg, responder, 0);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    fclose(file);
    free(filename);
    free(filepath);
    zmq_msg_close(&msg);
    zmq_close(responder);
    pthread_exit(NULL);
    return NULL;
}

int main(){
    printf("Starting client...\n");
    context = zmq_ctx_new();
    int no_files = recv_no_files();
    printf("Received number of files: %d\n", no_files);

    pthread_t threads[no_files];

    for (int i = 0; i < no_files; i++) {
        int *arg = malloc(sizeof(*arg));
        *arg = i;
        if(pthread_create(&threads[i], NULL, recv_file, arg) != 0){
            perror("Error creating thread");
            return -1;
        }
    }

    for (int i = 0; i < no_files; i++)
    {
        pthread_join(threads[i], NULL);
    }

    printf("Client sockets shutting down.\n");
    zmq_ctx_destroy(&context);
    return 0;
}
