/* SAE Réseau (Protocole TFTP) - Etape 3 Version Monothread - Licence 3 Info 2023-2024 */
/* MONTGNIE Yanis m22101878 - FOLLET Milo f22108222 - Groupe 2 */

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

typedef struct RequestInfo {
    FILE *fp;
    struct sockaddr_in addr;
    int expectedBlockNumber;
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
void addRequest(FILE *fp, struct sockaddr_in addr, int expectedBlockNumber);
RequestInfo *findRequest(struct sockaddr_in addr);
void removeRequest(RequestInfo *request);
void addClient(int sockfd, struct sockaddr_in addr, FILE *fp);
Client *findClient(struct sockaddr_in addr);
void removeClient(Client *client);

void handleRequest(int sockfd, fd_set *master_fds, int *max_fd);

void dieWithError(char *errorMessage) {
    perror(errorMessage);
    exit(EXIT_FAILURE);
}

void addRequest(FILE *fp, struct sockaddr_in addr, int expectedBlockNumber) {
    RequestInfo *new_request = malloc(sizeof(RequestInfo));
    new_request->fp = fp;
    new_request->addr = addr;
    new_request->expectedBlockNumber = expectedBlockNumber;
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

    char *filename = NULL; // Initialiser filename à NULL
    FILE *fp = NULL; // Ajouter la déclaration de fp ici
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
            printf("\n \n %d \n \n %d \n \n", buffer[1], n);
            if (n < 0)
                dieWithError("[ERROR] recvfrom error");

            printf("[INFO] Message received from %s:%d\n", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
            printf("%s", buffer);

            if (buffer[1] == 2) {
                char *filename = buffer + 2; // Mettre à jour la valeur de filename
                ackPacket[2] = 0;
                ackPacket[3] = 0;
                fp = fopen(filename, "wb");
                if (fp == NULL)
                    dieWithError("[ERROR] Cannot open file");

                addRequest(fp, addr, 0);

                if (sendto(sockfd, ackPacket, sizeof(ackPacket), 0, (struct sockaddr *)&addr, addr_size) < 0)
                    dieWithError("[ERROR] sendto error");

                Client *client = findClient(addr);
                if (client == NULL) {
                    addClient(sockfd, addr, fp);
                }

                FD_SET(fileno(fp), master_fds);
                *max_fd = (fileno(fp) > *max_fd) ? fileno(fp) : *max_fd;

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

                filename = buffer + 2; // Mettre à jour la valeur de filename
                ackPacket[2] = 0;
                ackPacket[3] = 0;
                fp = fopen(filename, "r");
                if (fp == NULL)
                    dieWithError("[ERROR] Cannot open file");

                addRequest(fp, addr, 0);

                memset(buffer, 0, SIZE);

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
                    break; // Sortir de la boucle après avoir envoyé le premier bloc
                }
                memset(buffer, 0, SIZE);
            }else if (buffer[1] == 4) {
            printf("reception d'un ACK\n");

            int receivedBlockNumber = (unsigned char)buffer[2] << 8 | (unsigned char)buffer[3];
            RequestInfo *request = findRequest(addr);
            if (request != NULL && receivedBlockNumber == request->expectedBlockNumber) {
                // L'ACK correspond au bloc attendu, mettre à jour expectedBlockNumber
                request->expectedBlockNumber++;
                // Vérifier si tous les blocs ont été envoyés avec succès
                printf("[INFO] Received ACK for block %d\n", receivedBlockNumber);

                readBytes = fread(buffer + 4, 1, 512, request->fp);

                retries = 0;

                if (readBytes > 0) {
                    while (retries < MAX_RETRIES) {
                        buffer[1] = 3;
                        buffer[2] = (request->expectedBlockNumber >> 8) & 0xFF;
                        buffer[3] = request->expectedBlockNumber & 0xFF;

                        n = sendto(sockfd, buffer, readBytes + 4, 0, (struct sockaddr *) &addr, sizeof(addr));
                        printf("%d",n);
                        if (n < 0) {
                            if (retries == MAX_RETRIES) {
                                fclose(request->fp);
                                dieWithError("[ERROR] sending block to the server after max retries.");
                            }
                            printf("[RETRY] Retrying sendto...\n");
                            retries++;
                            continue;
                        }
                        break; // Sortir de la boucle après avoir envoyé le bloc suivant
                    }
                } else {
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

        // Réinitialiser master_fds après avoir traité chaque requête
        FD_ZERO(&master_fds);
        FD_SET(server_sockfd, &master_fds);
        max_fd = server_sockfd;
    }

    return 0;
}
