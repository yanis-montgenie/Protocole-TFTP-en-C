#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <signal.h>
#include <errno.h>
#include <sys/select.h>

#define SIZE 516
#define MAX_CLIENT 10
#define MAX_RETRIES 3
#define MAX_OACK_SIZE 512

typedef struct RequestInfo {
    FILE *fp;
    struct sockaddr_in addr;
    int expectedBlockNumber;
    unsigned int bigfile;
    struct RequestInfo *next;
} RequestInfo;

typedef struct Client {
    int sockfd;
    struct sockaddr_in addr;
    FILE *fp;
    struct Client *next;
} Client;

RequestInfo *request_list = NULL;
Client *client_list = NULL;

void dieWithError(char *errorMessage);
RequestInfo *findRequest(struct sockaddr_in addr);
void removeRequest(RequestInfo *request);
void addClient(int sockfd, struct sockaddr_in addr, FILE *fp);
Client *findClient(struct sockaddr_in addr);
void removeClient(Client *client);
void addRequest(FILE *fp, struct sockaddr_in addr, int expectedBlockNumber, unsigned int bigfile);
void handleRequest(int sockfd, fd_set *master_fds, int *max_fd);

void dieWithError(char *errorMessage) {
    perror(errorMessage);
    exit(EXIT_FAILURE);
}

void addRequest(FILE *fp, struct sockaddr_in addr, int expectedBlockNumber, unsigned int bigfile) {
    RequestInfo *new_request = malloc(sizeof(RequestInfo));
    new_request->fp = fp;
    new_request->addr = addr;
    new_request->expectedBlockNumber = expectedBlockNumber;
    new_request->bigfile = bigfile;
    new_request->next = request_list;
    request_list = new_request;
}
RequestInfo *findRequest(struct sockaddr_in addr) {
    RequestInfo *current = request_list;
    while (current != NULL) {
        if (current->addr.sin_addr.s_addr == addr.sin_addr.s_addr && current->addr.sin_port == addr.sin_port) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

void removeRequest(RequestInfo *request) {
    if (request_list == request) {
        request_list = request->next;
    } else {
        RequestInfo *current = request_list;
        while (current->next != request) {
            current = current->next;
        }
        current->next = request->next;
    }
    free(request);
}

void addClient(int sockfd, struct sockaddr_in addr, FILE *fp) {
    Client *new_client = malloc(sizeof(Client));
    new_client->sockfd = sockfd;
    new_client->addr = addr;
    new_client->fp = fp;
    new_client->next = client_list;
    client_list = new_client;
}

Client *findClient(struct sockaddr_in addr) {
    Client *current = client_list;
    while (current != NULL) {
        if (current->addr.sin_addr.s_addr == addr.sin_addr.s_addr && current->addr.sin_port == addr.sin_port) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

void removeClient(Client *client) {
    if (client_list == client) {
        client_list = client->next;
    } else {
        Client *current = client_list;
        while (current->next != client) {
            current = current->next;
        }
        current->next = client->next;
    }
    free(client);
}

void createOackPacket(char *oackPacket, int *oackPacketLen, unsigned int bigfile) {
    oackPacket[0] = 0;
    oackPacket[1] = 6;
    int offset = 2;
    if (bigfile) {
        strcpy(&oackPacket[offset], "bigfile");
        offset += strlen("bigfile") + 1;
        memcpy(&oackPacket[offset], &bigfile, sizeof(bigfile));
        offset += sizeof(bigfile);
    }
    *oackPacketLen = offset;
}

void handleRequest(int sockfd, fd_set *master_fds, int *max_fd) {
    char buffer[SIZE];
    int n;
    struct sockaddr_in addr;
    socklen_t addr_size = sizeof(addr);
    char ackPacket[4];
    ackPacket[0] = 0;
    ackPacket[1] = 4;
    ackPacket[2] = 0;
    ackPacket[3] = 0;
    int readBytes, retries;
    RequestInfo *request;
    
    char *filename = NULL;
    FILE *fp = NULL;
    FD_SET(sockfd, master_fds);
    *max_fd = sockfd;
    
    while (1) {
        
        fd_set read_fds = *master_fds;
        
        int activity = select(*max_fd + 1, &read_fds, NULL, NULL, NULL);
        if (activity < 0)
            dieWithError("[ERROR] select error");
        
        
        if (FD_ISSET(sockfd, &read_fds)) {
            FILE *fp = NULL;
            
            n = recvfrom(sockfd, buffer, SIZE, 0, (struct sockaddr *)&addr, &addr_size);
            if (n < 0)
                dieWithError("[ERROR] recvfrom error");
            
            printf("[INFO] Message received from %s:%d\n", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
            printf("%s", buffer);
            
            
            
            
            if (buffer[1] == 2) {
                
                char *filename = buffer + 2;
                char* mode = filename + strlen(filename) + 1;
                char* option = mode + strlen(mode) + 1;
                ackPacket[2] = 0;
                ackPacket[3] = 0;
                
                fp = fopen(filename, "wb");
                if (fp == NULL)
                    dieWithError("[ERROR] Cannot open file");
                
                
                char *ptr = strstr(option, "bigfile");
                if (ptr != 0) {
                    
                    int bigfile = 1;
                    
                    addRequest(fp, addr, 0, bigfile);
                    
                    Client *client = findClient(addr);
                    if (client == NULL) {
                        addClient(sockfd, addr, fp);
                    }
                    
                    FD_SET(fileno(fp), master_fds);
                    
                    *max_fd = (fileno(fp) > *max_fd) ? fileno(fp) : *max_fd;
                    
                    char oackPacket[MAX_OACK_SIZE];
                    memset(oackPacket, 0, MAX_OACK_SIZE);
                    int oackPacketLen;
                    createOackPacket(oackPacket, &oackPacketLen, bigfile);
                    if (sendto(sockfd, oackPacket, oackPacketLen, 0, (struct sockaddr *)&addr, addr_size) < 0)
                        dieWithError("[ERROR] sendto error");
                    
                } else {
                    
                    int bigfile = 0;
                    
                    addRequest(fp, addr, 0, bigfile);
                    
                    if (sendto(sockfd, ackPacket, sizeof(ackPacket), 0, (struct sockaddr *)&addr, addr_size) < 0)
                        dieWithError("[ERROR] sendto error");
                    
                    Client *client = findClient(addr);
                    if (client == NULL) {
                        addClient(sockfd, addr, fp);
                    }
                    
                    FD_SET(fileno(fp), master_fds);
                    
                    *max_fd = (fileno(fp) > *max_fd) ? fileno(fp) : *max_fd;
                }
                
                
            } else if (buffer[1] == 3) {
                
                RequestInfo *request = findRequest(addr);
                if (request == NULL) {
                    printf("[WARNING] Request not found, ignoring data packet\n");
                    continue;
                }
                
                fp = request->fp;
                if (fwrite(buffer + 4, 1, n - 4, fp) < 1)
                    dieWithError("[ERROR] fwrite error");
                
                ackPacket[2] = buffer[2];
                ackPacket[3] = buffer[3];
                
                if (sendto(sockfd, ackPacket, sizeof(ackPacket), 0, (struct sockaddr *)&addr, addr_size) < 0)
                    dieWithError("[ERROR] sendto error");
                
                if (n < 512) {
                    printf("[SUCCESS] File received successfully.\n");
                    fclose(fp);
                    removeRequest(request);
                    
                    FD_CLR(fileno(fp), master_fds);
                    break;
                }
                
                
            } else if (buffer[1] == 1) {
                
                printf("[INFO] Message received from %s:%d\n", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
                char *filename = buffer + 2;
                char* mode = filename + strlen(filename) + 1;
                char* option = mode + strlen(mode) + 1;
                ackPacket[2] = 0;
                ackPacket[3] = 0;
                
                fp = fopen(filename, "r");
                if (fp == NULL)
                    dieWithError("[ERROR] Cannot open file");
                
                char *ptr = strstr(option, "bigfile");
                if (ptr != 0) {
                    
                    int bigfile = 1;
                    
                    
                    addRequest(fp, addr, 0, bigfile);
                    
                    
                    Client *client = findClient(addr);
                    if (client == NULL) {
                        addClient(sockfd, addr, fp);
                    }
                    
                    
                    char oackPacket[MAX_OACK_SIZE];
                    memset(oackPacket, 0, MAX_OACK_SIZE);
                    int oackPacketLen;
                    createOackPacket(oackPacket, &oackPacketLen, bigfile);
                    if (sendto(sockfd, oackPacket, oackPacketLen, 0, (struct sockaddr *)&addr, addr_size) < 0)
                        dieWithError("[ERROR] sendto error");
                    
                    
                    readBytes = fread(buffer + 4, 1, 512, fp);
                    
                    
                    int expectedBlockNumber = 0;
                    retries = 0;
                    
                    
                    while (retries < MAX_RETRIES) {
                        buffer[1] = 3;
                        buffer[2] = (expectedBlockNumber >> 8) & 0xFF;
                        buffer[3] = expectedBlockNumber & 0xFF;
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
                        break;
                    }
                    
                    memset(buffer, 0, SIZE);
                } else {
                    
                    
                    int bigfile = 0;
                    
                    
                    addRequest(fp, addr, 0, bigfile);
                    
                    
                    Client *client = findClient(addr);
                    if (client == NULL) {
                        addClient(sockfd, addr, fp);
                    }
                    
                    
                    readBytes = fread(buffer + 4, 1, 512, fp);
                    int expectedBlockNumber = 0;
                    retries = 0;
                    
                    while (retries < MAX_RETRIES) {
                        buffer[1] = 3;
                        buffer[2] = (expectedBlockNumber >> 8) & 0xFF;
                        buffer[3] = expectedBlockNumber & 0xFF;
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
                        break;
                    }
                    memset(buffer, 0, SIZE);
                }
                
                
            } else if (buffer[1] == 4) {
                
                int receivedBlockNumber = (unsigned char)buffer[2] << 8 | (unsigned char)buffer[3];
                
                RequestInfo *request = findRequest(addr);
                if (request != NULL && receivedBlockNumber == request->expectedBlockNumber) {
                    
                    
                    
                    printf("[INFO] Received ACK for block %d\n", receivedBlockNumber);
                    readBytes = fread(buffer + 4, 1, 512, request->fp);
                    retries = 0;
                    
                    while (retries < MAX_RETRIES) {
                        if (request->expectedBlockNumber == 65355 && request->bigfile == 1) {
                            request->expectedBlockNumber = 1;
                        } else {
                            request->expectedBlockNumber++;
                        }
                        buffer[1] = 3;
                        buffer[2] = (request->expectedBlockNumber >> 8) & 0xFF;
                        buffer[3] = request->expectedBlockNumber & 0xFF;
                        n = sendto(sockfd, buffer, readBytes + 4, 0, (struct sockaddr *) &addr, sizeof(addr));
                        if (n < 0) {
                            if (retries == MAX_RETRIES) {
                                fclose(request->fp);
                                dieWithError("[ERROR] sending block to the server after max retries.");
                            }
                            printf("[RETRY] Retrying sendto...\n");
                            retries++;
                            continue;
                        }
                        break;
                    }
                    
                    if (readBytes <= 0) {
                        printf("[SUCCESS] File sent successfully.\n");
                        fclose(request->fp);
                        removeRequest(request);
                    }
                }
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
    
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &sa, NULL);
    
    if (bind(server_sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
        dieWithError("[ERROR] bind error");
    
    printf("[STARTING] UDP File Server started on %s:%d.\n\n", ip, port);
    
    fd_set master_fds;
    int max_fd;
    
    while (1) {
        handleRequest(server_sockfd, &master_fds, &max_fd);
        
        
        FD_ZERO(&master_fds);
        FD_SET(server_sockfd, &master_fds);
        max_fd = server_sockfd;
    }
    
    return 0;
}
