#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <errno.h>
#include <netinet/in.h>

#define SIZE 516
#define TIMEOUT 5
#define MAX_RETRIES 3



void dieWithError(char *errorMessage) {
    perror(errorMessage);
    exit(EXIT_FAILURE);
}



int receiveServerResponse(int sockfd, int expectedBlockNumber, struct sockaddr_in *addr) {
    char ackBuffer[SIZE];
    socklen_t addr_size = sizeof(struct sockaddr_in);
    struct timeval timeout;
    
    
    timeout.tv_sec = TIMEOUT;
    timeout.tv_usec = 0;
    
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout)) < 0) {
        dieWithError("[ERROR] setsockopt() failed");
    }
    
    
    int n = recvfrom(sockfd, ackBuffer, sizeof(ackBuffer), 0, (struct sockaddr *) addr, &addr_size);
    
    if (n < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            printf("[ERROR] Timeout occurred while waiting for ACK.\n");
            return -1;
        }
        else {
            dieWithError("[ERROR] receiving ACK");
        }
    }
    
    
    if (ackBuffer[1] != 4) {
        
        if (ackBuffer[1] == 5) printf("[ERROR] Received error packet from server: %s\n", ackBuffer + 4);
        else printf("[ERROR] Undefined error\n");
        return -1;
    }
    
    
    int blockNumber = (unsigned char) ackBuffer[2] << 8 | (unsigned char) ackBuffer[3];
    
    
    if (blockNumber != expectedBlockNumber)
        dieWithError("[ERROR] Unexpected block number in ACK");
    
    printf("[INFO] Received ACK for block %d\n", blockNumber);
    return 0;
}



void send_WRQ(const char *fileName, int sockfd, struct sockaddr_in addr) {
    char buffer[SIZE];
    int n, readBytes, blockNumber = 0, retries;
    
    
    FILE *fp = fopen(fileName, "rb");
    
    if (fp == NULL) {
        dieWithError("[ERROR] Could not open file for reading");
    }
    
    
    memset(buffer, 0, SIZE);
    buffer[1] = 2;
    strcpy(buffer + 2, fileName);
    strcpy(buffer + 2 + strlen(fileName) + 1, "octet");
    
    
    int packetLength = 2 + strlen(fileName) + 1 + strlen("octet") + 1;
    
    
    n = sendto(sockfd, buffer, packetLength, 0, (struct sockaddr *) &addr, sizeof(addr));
    
    if (n < 0) {
        fclose(fp);
        dieWithError("[ERROR] sending WRQ to the server.");
    }
    
    
    if (receiveServerResponse(sockfd, 0, &addr) != 0) {
        
        fclose(fp);
        exit(EXIT_FAILURE);
    }
    
    
    memset(buffer, 0, SIZE);
    
    
    while ((readBytes = fread(buffer + 4, 1, 512, fp)) > 0) {
        blockNumber++;
        retries = 0;
        
        while (retries < MAX_RETRIES) {
            printf("[SENDING] Block %d\n", blockNumber);
            buffer[1] = 3;
            buffer[2] = (blockNumber >> 8) & 0xFF;
            buffer[3] = blockNumber & 0xFF;
            
            
            n = sendto(sockfd, buffer, readBytes + 4, 0, (struct sockaddr *) &addr, sizeof(addr));
            
            if (n < 0) {
                if (retries == MAX_RETRIES) {
                    fclose(fp);
                    dieWithError("[ERROR] sending block to the server after max retries.");
                }
                printf("[RETRY] Retrying sendto...\n");
                retries++;
                continue;
            }
            
            
            if (receiveServerResponse(sockfd, blockNumber, &addr) == 0)
                break;
            
            else {
                if (retries == MAX_RETRIES) {
                    fclose(fp);
                    dieWithError("[ERROR] No ACK received for block after max retries.");
                }
                printf("[RETRY] No ACK for block, retrying...\n");
                retries++;
            }
        }
        memset(buffer, 0, SIZE);
    }
    
    printf("[SUCCESS] File sent successfully.\n");
    fclose(fp);
}


void send_BIGWRQ(const char *fileName, int sockfd, struct sockaddr_in addr) {
    char buffer[SIZE];
    int n, readBytes, blockNumber = 0, retries;
    socklen_t addr_size = sizeof(struct sockaddr_in);
    
    
    FILE *fp = fopen(fileName, "rb");
    if (fp == NULL) {
        dieWithError("[ERROR] Could not open file for reading");
    }
    
    
    memset(buffer, 0, SIZE);
    buffer[1] = 2;
    strcpy(buffer + 2, fileName);
    strcpy(buffer + 2 + strlen(fileName) + 1, "octet");
    strcpy(buffer + 2 + strlen(fileName) + 1 + strlen("octet") + 1, "bigfile");
    int packetLength = 2 + strlen(fileName) + 1 + strlen("octet") + 1 + strlen("bigfile");
    
    n = sendto(sockfd, buffer, packetLength, 0, (struct sockaddr *)&addr, sizeof(addr));
    if (n < 0) {
        fclose(fp);
        dieWithError("[ERROR] sending WRQ to the server.");
    }
    
    
    
    n = recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr *)&addr, &addr_size);
    printf("[INFO] Reception \n %d", buffer[1]);
    if (n < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            printf("[ERROR] Timeout occurred while waiting for ACK.\n");
            fclose(fp);
            exit(EXIT_FAILURE);
        } else {
            fclose(fp);
            dieWithError("[ERROR] receiving ACK");
        }
    }
    
    if (buffer[1] == 6) {
        
        memset(buffer, 0, SIZE);
        
        while ((readBytes = fread(buffer + 4, 1, 512, fp)) > 0) {
            if (blockNumber < 65355){
                blockNumber++;
            }else{
                blockNumber = 1;
            }
            retries = 0;
            
            while (retries < MAX_RETRIES) {
                printf("[SENDING] Block %d\n", blockNumber);
                buffer[1] = 3;
                buffer[2] = (blockNumber >> 8) & 0xFF;
                buffer[3] = blockNumber & 0xFF;
                
                
                n = sendto(sockfd, buffer, readBytes + 4, 0, (struct sockaddr *) &addr, sizeof(addr));
                
                if (n < 0) {
                    if (retries == MAX_RETRIES) {
                        fclose(fp);
                        dieWithError("[ERROR] sending block to the server after max retries.");
                    }
                    printf("[RETRY] Retrying sendto...\n");
                    retries++;
                    continue;
                }
                
                
                if (receiveServerResponse(sockfd, blockNumber, &addr) == 0)
                    break;
                
                else {
                    if (retries == MAX_RETRIES) {
                        fclose(fp);
                        dieWithError("[ERROR] No ACK received for block after max retries.");
                    }
                    printf("[RETRY] No ACK for block, retrying...\n");
                    retries++;
                }
            }
            memset(buffer, 0, SIZE);
            
            
        }
        
        printf("[SUCCESS] File sent successfully.\n");
        fclose(fp);
    }else{
        fclose(fp);
        dieWithError("[ERROR] rejection of the option deal");
    }
}

void send_BIGRRQ(const char *fileName, int sockfd, struct sockaddr_in addr) {
    socklen_t addr_size = sizeof(struct sockaddr_in);
    addr_size = sizeof(addr);
    char buffer[SIZE];
    int n, retries;
    
    
    FILE *fp = fopen(fileName, "w+");
    
    if (fp == NULL)
        dieWithError("[ERROR] Could not open file for writing");
    
    
    struct timeval tv;
    tv.tv_sec = TIMEOUT;
    tv.tv_usec = 0;
    
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv) < 0)
        dieWithError("[ERROR] setsockopt failed");
    
    
    memset(buffer, 0, SIZE);
    buffer[1] = 1;
    strcpy(buffer + 2, fileName);
    strcpy(buffer + 2 + strlen(fileName) + 1, "octet");
    strcpy(buffer + 2 + strlen(fileName) + 1 + strlen("octet") + 1, "bigfile");
    
    int packetLength = 2 + strlen(fileName) + 1 + strlen("octet") + 1 + strlen("bigfile");
    
    
    n = sendto(sockfd, buffer, packetLength, 0, (struct sockaddr *)&addr, sizeof(addr));
    if (n < 0)
        dieWithError("[ERROR] sending RRQ to the server.");
    else if (n == 0) {
        printf("[INFO] No data sent to server.\n");
    }
    else {
        printf("[INFO] RRQ sent successfully.\n");
    }
    
    
    memset(buffer, 0, SIZE);
    
    char ackPacket[4];
    ackPacket[0] = 0;
    ackPacket[1] = 4;
    ackPacket[2] = 0;
    ackPacket[3] = 0;
    packetLength = 4;
    
    n = recvfrom(sockfd, buffer, SIZE, 0, (struct sockaddr *) &addr, &addr_size);
    
    
    if (n < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            if (retries == MAX_RETRIES) {
                dieWithError("[ERROR] recvfrom error after max retries");
            }
            printf("[RETRY] Retrying recvfrom...\n");
            retries++;
        }
        else {
            dieWithError("[ERROR] recvfrom error");
        }
    }
    
    if(buffer[1] == 6){
        sendto(sockfd, ackPacket, packetLength, 0, (struct sockaddr *) &addr, sizeof(addr));
    }
    else{
        dieWithError("[ERROR] : Server hasn't accepted the option.");
    }
    
    while (1) {
        retries = 0;
        
        while (retries < MAX_RETRIES) {
            
            n = recvfrom(sockfd, buffer, SIZE, 0, (struct sockaddr *) &addr, &addr_size);
            
            
            if (n < 0) {
                if (errno == EWOULDBLOCK || errno == EAGAIN) {
                    if (retries == MAX_RETRIES) {
                        dieWithError("[ERROR] recvfrom error after max retries");
                    }
                    printf("[RETRY] Retrying recvfrom...\n");
                    retries++;
                    continue;
                }
                else {
                    dieWithError("[ERROR] recvfrom error");
                }
            }
            
            
            if (buffer[1] == 3) {
                
                if (fwrite(buffer + 4, 1, n - 4, fp) < 1)
                    dieWithError("[ERROR] fwrite error");
                
                
                ackPacket[2] = buffer[2];
                ackPacket[3] = buffer[3];
                
                if (sendto(sockfd, ackPacket, sizeof(ackPacket), 0, (struct sockaddr *) &addr, addr_size) < 0)
                    dieWithError("[ERROR] sendto error");
                
                break;
            }
            
            else if (buffer[1]==5){
                printf("[ERROR] Received error packet from server: %s\n", buffer + 4);
                fclose(fp);
                exit(EXIT_FAILURE);
            }
        }
        
        
        if (n < 512) {
            if (retries == MAX_RETRIES){
                printf("[ERROR] Max retries attempt. File cannot be received");
            }
            else {
                printf("[SUCCESS] File received successfully.\n");
            }
            
            fclose(fp);
            break;
        }
    }
}



void send_RRQ(const char *fileName, int sockfd, struct sockaddr_in addr) {
    socklen_t addr_size = sizeof(struct sockaddr_in);
    addr_size = sizeof(addr);
    char buffer[SIZE];
    int n, retries;
    
    
    FILE *fp = fopen(fileName, "w+");
    
    if (fp == NULL)
        dieWithError("[ERROR] Could not open file for writing");
    
    
    struct timeval tv;
    tv.tv_sec = TIMEOUT;
    tv.tv_usec = 0;
    
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv) < 0)
        dieWithError("[ERROR] setsockopt failed");
    
    
    memset(buffer, 0, SIZE);
    buffer[1] = 1;
    strcpy(buffer + 2, fileName);
    strcpy(buffer + 2 + strlen(fileName) + 1, "octet");
    
    
    int packetLength = 2 + strlen(fileName) + 1 + strlen("octet") + 1;
    
    
    n = sendto(sockfd, buffer, packetLength, 0, (struct sockaddr *) &addr, sizeof(addr));
    if (n < 0)
        dieWithError("[ERROR] sending RRQ to the server.");
    else if (n == 0) {
        printf("[INFO] No data sent to server.\n");
    }
    else {
        printf("[INFO] RRQ sent successfully.\n");
    }
    
    
    memset(buffer, 0, SIZE);
    
    char ackPacket[4];
    ackPacket[0] = 0;
    ackPacket[1] = 4;
    ackPacket[2] = 0;
    ackPacket[3] = 0;
    
    
    
    while (1) {
        retries = 0;
        
        while (retries < MAX_RETRIES) {
            
            n = recvfrom(sockfd, buffer, SIZE, 0, (struct sockaddr *) &addr, &addr_size);
            
            
            if (n < 0) {
                if (errno == EWOULDBLOCK || errno == EAGAIN) {
                    if (retries == MAX_RETRIES) {
                        dieWithError("[ERROR] recvfrom error after max retries");
                    }
                    printf("[RETRY] Retrying recvfrom...\n");
                    retries++;
                    continue;
                }
                else {
                    dieWithError("[ERROR] recvfrom error");
                }
            }
            
            
            if (buffer[1] == 3) {
                
                if (fwrite(buffer + 4, 1, n - 4, fp) < 1)
                    dieWithError("[ERROR] fwrite error");
                
                
                ackPacket[2] = buffer[2];
                ackPacket[3] = buffer[3];
                
                if (sendto(sockfd, ackPacket, sizeof(ackPacket), 0, (struct sockaddr *) &addr, addr_size) < 0)
                    dieWithError("[ERROR] sendto error");
                
                break;
            }
            
            else if (buffer[1]==5){
                printf("[ERROR] Received error packet from server: %s\n", buffer + 4);
                fclose(fp);
                exit(EXIT_FAILURE);
            }
        }
        
        
        if (n < 512) {
            if (retries == MAX_RETRIES){
                printf("[ERROR] Max retries attempt. File cannot be received");
            }
            else {
                printf("[SUCCESS] File received successfully.\n");
            }
            
            fclose(fp);
            break;
        }
    }
}


int main(int argc, char* argv[]) {
    char ip[15], filename[256], request[4], option[50];
    int port;
    
    
    
    strcpy(ip,"127.0.0.1");
    port = 8080;
    
    
    if (argc < 3) {
        printf("[ERROR] Invalid arguments\n");
        exit(EXIT_FAILURE);
    }
    
    
    strcpy(request, argv[1]);
    strcpy(filename, argv[2]);
    if (argc > 3) {
        strcpy(option, argv[3]);
    } else {
        
        
        strcpy(option, "");
        
        
    }
    
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in server_addr;
    
    if (sockfd < 0)
        dieWithError("[ERROR] socket error");
    
    
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(ip);
    
    
    if (strcmp("put", request) == 0) {
        if (argc > 3) {
            if (strcmp("bigfile", option) == 0) {
                send_BIGWRQ(filename, sockfd, server_addr);
            }
        }
        else {
            
            send_WRQ(filename, sockfd, server_addr);
        }
    }
    else if (strcmp("get", request) == 0) {
        if (argc > 3) {
            if (strcmp("bigfile", option) == 0) {
                send_BIGRRQ(filename, sockfd, server_addr);
            }
        }else{
            
            send_RRQ(filename, sockfd, server_addr);
        }
    }
    else {
        dieWithError("[ERROR] Invalid request type");
    }
    
    
    close(sockfd);
    return 0;
}
