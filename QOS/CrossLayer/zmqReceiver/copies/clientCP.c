#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zmq.h>
#include <unistd.h>
#include <stdbool.h>


void *context;

int main(){
    printf("Starting client...\n");
    context = zmq_ctx_new();
    void *responder = zmq_socket(context, ZMQ_REP);
    bool enable = true;

    int expected_files = 4;
    int received_files = 0;

    if(zmq_bind(responder, "tcp://0.0.0.0:4444") != 0){
        fprintf(stderr, "Failed to bind socket: %s\n", zmq_strerror(zmq_errno()));
        enable = false;
    }

    while(enable){
        zmq_msg_t msg;
        zmq_msg_init(&msg);
        zmq_msg_recv(&msg, responder, 0);
        char *data = zmq_msg_data(&msg);
        int length = strlen(data);
        char *filename = (char *) malloc(length + 1);
        strcpy(filename, data);
        printf("Received filename: %s\n", filename);

        zmq_msg_init(&msg);
        zmq_msg_recv(&msg, responder, 0);
        long file_size = zmq_msg_size(&msg);
        char *buffer = (char *) malloc(file_size);
        if(buffer == NULL){
            fprintf(stderr, "Failed to allocate memory for file buffer\n");
            free(filename);
            zmq_msg_close(&msg);
            continue;
        }
        memcpy(buffer, zmq_msg_data(&msg), file_size);
        printf("Received file content of size: %ld\n", file_size);
  	
	char filepath[256];
	char new_directory[256];
	char directory[128] = "./data";
	if(strstr(filename, "reduced") != NULL){
		snprintf(new_directory, sizeof(new_directory), "%s/%s/", directory, "reduced");
	}
	else if(strstr(filename, "delta") != NULL)
	{
		snprintf(new_directory, sizeof(new_directory), "%s/%s/", directory, "delta");
	}else {
		snprintf(new_directory, sizeof(new_directory), "%s/", directory);
	}

	sprintf(filepath, "%s%s", new_directory, filename);

  	FILE *file = fopen(filepath, "wb");
        if(file == NULL){
            fprintf(stderr, "Failed to open file %s\n", filename);
            free(buffer);
            free(filename);
            continue;
        }
        fwrite(buffer, 1, file_size, file);
        fclose(file);

        int ackMsgSize = snprintf(NULL, 0, "File %s received successfully!", filename);
        char *ackMsg = (char *) malloc(ackMsgSize + 1);
        snprintf(ackMsg, ackMsgSize ,"File %s received successfully!", filename);
        zmq_msg_init_size(&msg, ackMsgSize);
        zmq_msg_init_data(&msg, ackMsg, ackMsgSize, NULL, NULL);
        zmq_msg_send(&msg, responder, 0);

        free(filename);
        free(buffer);
        zmq_msg_close(&msg);
        received_files++;
        if(received_files == expected_files){
           break;
        }
    }
    printf("Client sockets shutting down.\n");
    zmq_ctx_destroy(&context);
    return 0;
}
