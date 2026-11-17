#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define SIZE 516
#define TIMEOUT 5


void dieWithError(char *errorMessage){
    perror(errorMessage);
    exit(EXIT_FAILURE);
}



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



void handleRequest(int sockfd, struct sockaddr_in addr){
    char buffer[SIZE];
    FILE *fp = NULL;
    socklen_t addr_size;
    int n;
    
    char ackPacket[4];
    ackPacket[0] = 0;
    ackPacket[1] = 4;
    ackPacket[2] = 0;
    ackPacket[3] = 0;
    
    while (1){
        addr_size = sizeof(addr);
        n = recvfrom(sockfd, buffer, SIZE, 0, (struct sockaddr *)&addr, &addr_size);
        if (n < 0)
            dieWithError("[ERROR] recvfrom error");
        
        
        if (buffer[1] == 2){
            char *filename = buffer + 2;
            
            if (access(filename, F_OK) != -1) {
                perror("[ERROR] File already exists");
                
                buffer[1] = 5;
                buffer[3] = 6;
                strcpy(buffer + 4, "File already exists");
                
                if (sendto(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr *)&addr, addr_size) < 0)
                    dieWithError("[ERROR] sendto error");
                
                continue;
            }
            
            
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
                
                
                n = sendto(sockfd, buffer, readBytes + 4, 0, (struct sockaddr *)&addr, sizeof(addr));
                
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


int main(){
    
    char *ip = "127.0.0.1";
    int port = 8080;
    
    
    int server_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (server_sockfd < 0)
        dieWithError("[ERROR] socket error");
    
    
    struct sockaddr_in server_addr, client_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(ip);
    
    
    if (bind(server_sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
        dieWithError("[ERROR] bind error");
    
    printf("[STARTING] UDP File Server started on %s:%d.\n\n", ip, port);
    
    
    while (1)
        handleRequest(server_sockfd, client_addr);
    
    return 0;
}
