#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zmq.h>
#include <pthread.h>

void *context;

void* send_file(void *args){
    const char *directory = "./data/";
    const char *filename = (const char*) args;
    char filepath[256];
    sprintf(filepath, "%s%s", directory, filename);
    
    void *requester = zmq_socket(context, ZMQ_REQ);

    if(zmq_connect(requester, "tcp://129.114.108.224:4444") != 0){
        fprintf(stderr, "Failed to connect to server: %s\n", zmq_strerror(zmq_errno()));
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

    zmq_msg_t msg;
    zmq_msg_init_size(&msg, strlen(filename) + 1);
    zmq_msg_init_data(&msg, filename, strlen(filename) + 1, NULL, NULL);
    zmq_msg_send(&msg, requester, ZMQ_SNDMORE);
    zmq_msg_close(&msg);

    char *buffer = (char*) malloc(file_size);
    if(buffer == NULL){
        fprintf(stderr, "Failed to allocate memory\n");
        fclose(file);
        return NULL;
    } 
    
    fread(buffer, 1, file_size, file);
    fclose(file);
    zmq_msg_init_size(&msg, file_size);
    zmq_msg_init_data(&msg, buffer, file_size, NULL, NULL);
    zmq_msg_send(&msg, requester, 0);

    zmq_msg_init(&msg);
    zmq_msg_recv(&msg, requester, 0);

    if(strncmp)
    printf("Received acknowledgment: %s\n", (char*) zmq_msg_data(&msg));

    free(buffer);
    zmq_msg_close(&msg);
    pthread_exit(NULL);
    return NULL;
}

int main(){
    context = zmq_ctx_new();
    const char *filenames[] = {"delta_r_xgc_o.bin", "delta_z_xgc_o.bin", "delta_xgc_o.bin", "reduced_data_xgc_16.bin"};
    const int num_threads = sizeof(filenames) / sizeof(filenames[0]);
    printf("Number of threads: %d\n", num_threads);
    const pthread_t threads[num_threads];
    int threads_id[num_threads];
    for(int i = 0; i < num_threads; i++){
        threads_id[i] = i+1;
        if(pthread_create(&threads[i], NULL, send_file, (void *)filenames[i]) != 0){
            perror("Error creating thread");
            return -1;
        }
    }

    for (int i = 0; i < num_threads; i++)
    {
        if(pthread_join(threads[i], NULL) != 0){
            perror("Error joining thread");
            return -1;
        }
    }

    printf("Sending files completed\n");
    zmq_ctx_destroy(context);
    return 0;
}dddd
