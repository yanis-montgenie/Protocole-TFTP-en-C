#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <pthread.h>

#define SIZE 516
#define TIMEOUT 5
#define MAX_RETRIES 5
#define MAX_CLIENT 15

pthread_t threads[MAX_CLIENT];
int num_clients = 0;
pthread_mutex_t client_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;



typedef struct {
    int sockfd;
    struct sockaddr_in addr;
    char data[SIZE];
    int len;
} ClientArgs;


void dieWithError(char *errorMessage, int sockfd) {
    if (sockfd >= 0) close(sockfd);
    perror(errorMessage);
    printf("[CLOSING] Closing the server.\n");
    exit(EXIT_FAILURE);
}



void sendErrorPacket(int sockfd, struct sockaddr_in *addr, int errorCode, const char *errorMessage) {
    char errorBuffer[SIZE];
    int errorMessageLength = strlen(errorMessage) + 1;
    errorBuffer[0] = 0;
    errorBuffer[1] = 5;
    errorBuffer[2] = (errorCode >> 8) & 0xFF;
    errorBuffer[3] = errorCode & 0xFF;
    strcpy(errorBuffer + 4, errorMessage);
    
    sendto(sockfd, errorBuffer, 4 + errorMessageLength, 0, (struct sockaddr *)addr, sizeof(struct sockaddr_in));
}



int receiveACK(int sockfd, int expectedBlockNumber, struct sockaddr_in *addr){
    char ackBuffer[4];
    socklen_t addr_size = sizeof(struct sockaddr_in);
    
    
    int n = recvfrom(sockfd, ackBuffer, sizeof(ackBuffer), 0, (struct sockaddr *)addr, &addr_size);
    
    if (n < 0)
        printf("[ERROR] receiving ACK");
    
    
    if (ackBuffer[1] != 4)
        printf("[ERROR] Not an ACK packet");
    
    
    int blockNumber = (unsigned char)ackBuffer[2] << 8 | (unsigned char)ackBuffer[3];
    
    
    if (blockNumber != expectedBlockNumber)
        printf("[ERROR] Unexpected block number in ACK");
    
    printf("[INFO] Received ACK for block %d\n", blockNumber);
    return 0;
}

void* handleRequest(void* args) {
    pthread_mutex_lock(&file_mutex);
    ClientArgs* clientArgs = (ClientArgs*)args;
    
    int sockfd = clientArgs->sockfd;
    struct sockaddr_in addr = clientArgs->addr;
    int len = clientArgs->len;
    
    char buffer[SIZE];
    FILE *fp = NULL;
    socklen_t addr_size = sizeof(struct sockaddr_in);
    int n, readBytes, blockNumber, retries, ackReceived, running = 1;
    char ackPacket[4] = {0, 4, 0, 0};
    char *filename;
    
    memcpy(buffer, clientArgs->data, len);
    free(args);
    
    while (running) {
        
        
        if (buffer[1] == 2){
            char *filename = buffer + 2;
            
            if (access(filename, F_OK) != -1) {
                perror("[ERROR] File already exists");
                
                buffer[1] = 5;
                buffer[3] = 6;
                strcpy(buffer + 4, "File already exists");
                
                if (sendto(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr *)&addr, addr_size) < 0)
                    printf("[ERROR] sendto error");
                
                continue;
            }
            
            
            fp = fopen(filename, "wb");
            if (fp == NULL) {
                
                sendErrorPacket(sockfd, &addr, 2, "Cannot create file.");
                running = 0;
                break;
            }
            
            
            if (sendto(sockfd, ackPacket, sizeof(ackPacket), 0, (struct sockaddr *)&addr, addr_size) < 0) {
                printf("[ERROR] sendto error\n");
                running = 0;
            }
            break;
        }
        
        else if (buffer[1] == 3){
            
            if (fwrite(buffer + 4, 1, n - 4, fp) < 1){
                printf("[ERROR] fwrite error\n");
                running = 0;
                break;
            }
            
            
            ackPacket[2] = buffer[2];
            ackPacket[3] = buffer[3];
            
            
            if (sendto(sockfd, ackPacket, sizeof(ackPacket), 0, (struct sockaddr *)&addr, addr_size) < 0) {
                printf("[ERROR] sendto error\n");
                running = 0;
                break;
            }
            
            
            if (n < 512){
                printf("[SUCCESS] File received successfully.\n");
                fclose(fp);
                running = 0;
                break;
            }
            break;
        }
        
        else if (buffer[1] == 1){
            int readBytes, blockNumber = 0;
            char *filename = buffer + 2;
            
            
            fp = fopen(filename, "r");
            if (fp == NULL){
                perror("[ERROR] Cannot open file");
                
                buffer[1] = 5;
                buffer[3] = 1;
                strcpy(buffer + 4, "File not found");
                
                if (sendto(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr *)&addr, addr_size) < 0)
                    printf("[ERROR] sendto error");
                
                running = 0;
                break;
            }
            
            
            memset(buffer, 0, SIZE);
            
            
            while ((readBytes = fread(buffer + 4, 1, 512, fp)) > 0){
                blockNumber++;
                printf("[SENDING] Block %d\n", blockNumber);
                buffer[1] = 3;
                
                
                buffer[2] = (blockNumber >> 8) & 0xFF;
                buffer[3] = blockNumber & 0xFF;
                
                
                n = sendto(sockfd, buffer, readBytes + 4, 0, (struct sockaddr *)&addr, sizeof(addr));
                
                if (n < 0)
                    printf("[ERROR] sending block to the server.");
                
                
                receiveACK(sockfd, blockNumber, &addr);
                
                memset(buffer, 0, SIZE);
            }
            
            printf("[SUCCESS] File sent successfully.\n");
            fclose(fp);
            running = 0;
            break;
        }
        if (running) {
            memset(buffer, 0, SIZE);
            addr_size = sizeof(addr);
            n = recvfrom(sockfd, buffer, SIZE, 0, (struct sockaddr *)&addr, &addr_size);
            if (n < 0) {
                printf("[ERROR] recvfrom error\n");
                running = 0;
            }
        }
    }
    
    if (fp != NULL) fclose(fp);
    pthread_mutex_unlock(&file_mutex);
    pthread_exit(NULL);
}

int main() {
    
    char *ip = "127.0.0.1";
    int port = 8080;
    
    
    int server_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (server_sockfd < 0) {
        perror("[ERROR] socket error");
        exit(EXIT_FAILURE);
    }
    
    
    struct sockaddr_in server_addr, client_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(ip);
    
    
    int r = bind(server_sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    
    if (r < 0) dieWithError("[ERROR] Bind error", server_sockfd);
    
    char buffer[SIZE];
    socklen_t addr_len = sizeof(client_addr);
    
    printf("[STARTING] UDP File Server started on %s:%d.\n", ip, port);
    
    while(1) {
        sleep(1);
        printf("[INFO] Waiting for requests...\n");
        
        r = recvfrom(server_sockfd, buffer, SIZE, 0, (struct sockaddr *)&client_addr, &addr_len);
        if (r < 0) continue;
        
        if (buffer[1] == 1 || buffer[1] == 2) {
            
            ClientArgs* clientArgs = (ClientArgs*)malloc(sizeof(ClientArgs));
            if (!clientArgs) continue;
            
            clientArgs->sockfd = server_sockfd;
            clientArgs->addr = client_addr;
            memcpy(clientArgs->data, buffer, r);
            clientArgs->len = r;
            
            
            pthread_mutex_lock(&client_mutex);
            if (num_clients < MAX_CLIENT) {
                if (pthread_create(&threads[num_clients++], NULL, handleRequest, (void*)clientArgs) != 0) {
                    perror("[ERROR] Failed to create thread");
                    free(clientArgs);
                }
            } else {
                printf("[INFO] Maximum number of clients reached. Dropping request.\n");
                free(clientArgs);
            }
            pthread_mutex_unlock(&client_mutex);
        }
        
    }
    
    printf("[CLOSING] Closing the server.\n");
    
    close(server_sockfd);
    
    return 0;
}
