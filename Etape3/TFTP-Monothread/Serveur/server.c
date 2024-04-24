#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>

#define SIZE 516
#define MAX_CLIENT 10

void dieWithError(char *errorMessage) {
    perror(errorMessage);
    exit(EXIT_FAILURE);
}


typedef struct {
    struct sockaddr_in addr;
    socklen_t addr_size;
} ClientInfo;


int receiveACK(int sockfd, int expectedBlockNumber, struct sockaddr_in *addr){
    char ackBuffer[4];
    socklen_t addr_size = sizeof(struct sockaddr_in);
    
    
    int n = recvfrom(sockfd, ackBuffer, sizeof(ackBuffer), 0, (struct sockaddr *)addr, &addr_size);
    
    if (n < 0)
        dieWithError("[ERROR] receiving ACK");
    
    
    if (ackBuffer[1] != 4)
        dieWithError("[ERROR] Not an ACK packet");
    
    
    int blockNumber = (unsigned char)ackBuffer[2] << 8 | (unsigned char)ackBuffer[3];
    
    
    if (blockNumber != expectedBlockNumber)
        dieWithError("[ERROR] Unexpected block number in ACK");
    
    printf("[INFO] Received ACK for block %d\n", blockNumber);
    return 0;
}



void handleRequest(int sockfd, ClientInfo *clients, int *num_clients) {
    char buffer[SIZE];
    FILE *fp = NULL;
    int n;
    printf("heuuu");
    char ackPacket[4];
    ackPacket[0] = 0;
    ackPacket[1] = 4;
    ackPacket[2] = 0;
    ackPacket[3] = 0;
    
    while (1) {
        struct sockaddr_in addr;
        socklen_t addr_size = sizeof(addr);
        
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);
        
        
        int activity = select(sockfd + 1, &readfds, NULL, NULL, NULL);
        if (activity < 0)
            dieWithError("[ERROR] select error");
        
        if (FD_ISSET(sockfd, &readfds)) {
            
            n = recvfrom(sockfd, buffer, SIZE, 0, (struct sockaddr *)&addr, &addr_size);
            if (n < 0)
                dieWithError("[ERROR] recvfrom error");
            
            printf("[INFO] Message received from %s:%d\n", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
            printf("%s", buffer);
            
            
            if (buffer[1] == 2){
                printf("[INFO] Message received from %s:%d\n", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
                char *filename = buffer + 2;
                
                
                fp = fopen(filename, "wb");
                if (fp == NULL)
                    dieWithError("[ERROR] Cannot open file");
                
                
                if (sendto(sockfd, ackPacket, sizeof(ackPacket), 0, (struct sockaddr *)&addr, addr_size) < 0)
                    dieWithError("[ERROR] sendto error");
                
            }
            
            else if (buffer[1] == 3){
                
                if (fwrite(buffer + 4, 1, n - 4, fp) < 1)
                    dieWithError("[ERROR] fwrite error");
                
                
                ackPacket[2] = buffer[2];
                ackPacket[3] = buffer[3];
                
                
                if (sendto(sockfd, ackPacket, sizeof(ackPacket), 0, (struct sockaddr *)&addr, addr_size) < 0)
                    dieWithError("[ERROR] sendto error");
                
                
                if (n < 512){
                    printf("[SUCCESS] File received successfully.\n");
                    fclose(fp);
                    break;
                }
            }
            
            else if (buffer[1] == 1){
                printf("[INFO] Message received from %s:%d\n", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
                int readBytes, blockNumber = 0;
                char *filename = buffer + 2;
                
                
                fp = fopen(filename, "r");
                if (fp == NULL){
                    perror("[ERROR] Cannot open file");
                    
                    buffer[1] = 5;
                    buffer[3] = 1;
                    strcpy(buffer + 4, "File not found");
                    
                    if (sendto(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr *)&addr, addr_size) < 0)
                        dieWithError("[ERROR] sendto error");
                    
                    continue;
                }
                
                
                memset(buffer, 0, SIZE);
                
                
                while ((readBytes = fread(buffer + 4, 1, 512, fp)) > 0){
                    blockNumber++;
                    printf("[SENDING] Block %d\n", blockNumber);
                    buffer[1] = 3;
                    
                    
                    buffer[2] = (blockNumber >> 8) & 0xFF;
                    buffer[3] = blockNumber & 0xFF;
                    
                    
                    n = sendto(sockfd, buffer, readBytes + 4, 0, (struct sockaddr *)&addr, addr_size);
                    printf("[INFO] Message received from %s:%d\n", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
                    if (n < 0)
                        dieWithError("[ERROR] sending block to the server.");
                    
                    
                    receiveACK(sockfd, blockNumber, &addr);
                    
                    memset(buffer, 0, SIZE);
                }
                
                printf("[SUCCESS] File sent successfully.\n");
                fclose(fp);
                break;
            }
        }
    }
}


int main() {
    
    char *ip = "127.0.0.1";
    int port = 8080;
    
    
    int server_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (server_sockfd < 0)
        dieWithError("[ERROR] socket error");
    
    
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(ip);
    
    
    if (bind(server_sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
        dieWithError("[ERROR] bind error");
    
    printf("[STARTING] UDP File Server started on %s:%d.\n\n", ip, port);
    
    ClientInfo clients[MAX_CLIENT];
    int num_clients = 0;
    
    
    while (1) {
        handleRequest(server_sockfd, clients, &num_clients);
    }
    
    return 0;
}
