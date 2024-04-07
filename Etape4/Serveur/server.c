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
    unsigned int bigfile; //  ce champ sert pour stocker la valeur de l'option bigfile
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
    new_request->bigfile = bigfile; // Initialisez le champ bigfile
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
    oackPacket[1] = 6; // Code OACK
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
    FD_SET(sockfd, master_fds);  // Ajouter sockfd à l'ensemble des descripteurs surveillés
    *max_fd = sockfd;

    while (1) {
        // Copier l'ensemble des descripteurs surveillés dans une variable temporaire
        fd_set read_fds = *master_fds;
        // Attendre une activité sur l'un des descripteurs surveillés
        int activity = select(*max_fd + 1, &read_fds, NULL, NULL, NULL);
        if (activity < 0)
            dieWithError("[ERROR] select error");

        // Vérifier si le descripteur du socket est prêt à être lu
        if (FD_ISSET(sockfd, &read_fds)) {
            FILE *fp = NULL;
            // Recevoir des données du client
            n = recvfrom(sockfd, buffer, SIZE, 0, (struct sockaddr *)&addr, &addr_size);
            if (n < 0)
                dieWithError("[ERROR] recvfrom error");

            printf("[INFO] Message received from %s:%d\n", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
            printf("%s", buffer);

            // Gérer les différents types de paquets TFTP

            // Cas où le paquet est une demande d'écriture (WRQ)
            if (buffer[1] == 2) {
                // Extraire le nom du fichier, le mode et l'option "bigfile" de la demande
                char *filename = buffer + 2;
                char* mode = filename + strlen(filename) + 1;
                char* option = mode + strlen(mode) + 1;
                ackPacket[2] = 0;
                ackPacket[3] = 0;
                // Ouvrir le fichier en mode écriture binaire
                fp = fopen(filename, "wb");
                if (fp == NULL)
                    dieWithError("[ERROR] Cannot open file");

                // Vérifier si l'option "bigfile" est présente dans la demande
                char *ptr = strstr(option, "bigfile");
                if (ptr != 0) {
                    // Le fichier est un "bigfile"
                    int bigfile = 1;
                    // Ajouter la demande à la liste des demandes en attente
                    addRequest(fp, addr, 0, bigfile);
                    // Ajouter le client à la liste des clients actifs
                    Client *client = findClient(addr);
                    if (client == NULL) {
                        addClient(sockfd, addr, fp);
                    }
                    // Ajouter le descripteur du fichier à l'ensemble des descripteurs surveillés
                    FD_SET(fileno(fp), master_fds);
                    // Mettre à jour la valeur maximale du descripteur surveillé
                    *max_fd = (fileno(fp) > *max_fd) ? fileno(fp) : *max_fd;
                    // Créer le paquet OACK et l'envoyer au client
                    char oackPacket[MAX_OACK_SIZE];
                    memset(oackPacket, 0, MAX_OACK_SIZE);
                    int oackPacketLen;
                    createOackPacket(oackPacket, &oackPacketLen, bigfile);
                    if (sendto(sockfd, oackPacket, oackPacketLen, 0, (struct sockaddr *)&addr, addr_size) < 0)
                        dieWithError("[ERROR] sendto error");

                } else {
                    // Le fichier n'est pas un "bigfile"
                    int bigfile = 0;
                    // Ajouter la demande à la liste des demandes en attente
                    addRequest(fp, addr, 0, bigfile);
                    // Envoyer l'accusé de réception au client
                    if (sendto(sockfd, ackPacket, sizeof(ackPacket), 0, (struct sockaddr *)&addr, addr_size) < 0)
                        dieWithError("[ERROR] sendto error");
                    // Ajouter le client à la liste des clients actifs
                    Client *client = findClient(addr);
                    if (client == NULL) {
                        addClient(sockfd, addr, fp);
                    }
                    // Ajouter le descripteur du fichier à l'ensemble des descripteurs surveillés
                    FD_SET(fileno(fp), master_fds);
                    // Mettre à jour la valeur maximale du descripteur surveillé
                    *max_fd = (fileno(fp) > *max_fd) ? fileno(fp) : *max_fd;
                }

            // Cas où le paquet est une paquet de données (DATA)
            } else if (buffer[1] == 3) {
                // Trouver la demande associée à l'adresse du client
                RequestInfo *request = findRequest(addr);
                if (request == NULL) {
                    printf("[WARNING] Request not found, ignoring data packet\n");
                    continue;
                }
                // Écrire les données dans le fichier associé à la demande
                fp = request->fp;
                if (fwrite(buffer + 4, 1, n - 4, fp) < 1)
                    dieWithError("[ERROR] fwrite error");
                // Mettre à jour les numéros de bloc dans l'accusé de réception
                ackPacket[2] = buffer[2];
                ackPacket[3] = buffer[3];
                // Envoyer l'accusé de réception au client
                if (sendto(sockfd, ackPacket, sizeof(ackPacket), 0, (struct sockaddr *)&addr, addr_size) < 0)
                    dieWithError("[ERROR] sendto error");
                // Vérifier si la réception est terminée
                if (n < 512) {
                    printf("[SUCCESS] File received successfully.\n");
                    fclose(fp);
                    removeRequest(request);
                    // Retirer le descripteur du fichier de l'ensemble des descripteurs surveillés
                    FD_CLR(fileno(fp), master_fds);
                    break;
                }

            // Cas où le paquet est une demande de lecture (RRQ)
            } else if (buffer[1] == 1) {
                // Extraire le nom du fichier, le mode et l'option "bigfile" de la demande
                printf("[INFO] Message received from %s:%d\n", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
                char *filename = buffer + 2;
                char* mode = filename + strlen(filename) + 1;
                char* option = mode + strlen(mode) + 1;
                ackPacket[2] = 0;
                ackPacket[3] = 0;
                // Ouvrir le fichier en mode lecture
                fp = fopen(filename, "r");
                if (fp == NULL)
                    dieWithError("[ERROR] Cannot open file");
                // Vérifier si l'option "bigfile" est présente dans la demande
                char *ptr = strstr(option, "bigfile");
                if (ptr != 0) {// Le fichier est un "bigfile"
                    
                    int bigfile = 1;

                    // Ajouter la demande à la liste des demandes en attente
                    addRequest(fp, addr, 0, bigfile);

                    // Ajouter le client à la liste des clients actifs
                    Client *client = findClient(addr);
                    if (client == NULL) {
                        addClient(sockfd, addr, fp);
                    }

                    // Créer le paquet OACK et l'envoyer au client
                    char oackPacket[MAX_OACK_SIZE];
                    memset(oackPacket, 0, MAX_OACK_SIZE);
                    int oackPacketLen;
                    createOackPacket(oackPacket, &oackPacketLen, bigfile);
                    if (sendto(sockfd, oackPacket, oackPacketLen, 0, (struct sockaddr *)&addr, addr_size) < 0)
                        dieWithError("[ERROR] sendto error");

                    // Lire les premières données du fichier
                    readBytes = fread(buffer + 4, 1, 512, fp);

                    //initialiser le numéro du premier block et le nombre d'essaie d'envoie des données
                    int expectedBlockNumber = 0;
                    retries = 0;

                    // Envoyer les données au client
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
                    //on remet le buffer a 0
                    memset(buffer, 0, SIZE);
                } else {

                    // Le fichier n'est pas un "bigfile"
                    int bigfile = 0;

                    // Ajouter la demande à la liste des demandes en attente
                    addRequest(fp, addr, 0, bigfile);

                    // Ajouter le client à la liste des clients actifs
                    Client *client = findClient(addr);
                    if (client == NULL) {
                        addClient(sockfd, addr, fp);
                    }

                    // Lire les premières données du fichier
                    readBytes = fread(buffer + 4, 1, 512, fp);
                    int expectedBlockNumber = 0;
                    retries = 0;
                    // Envoyer les données au client
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

            // Cas où le paquet est un accusé de réception (ACK)
            } else if (buffer[1] == 4) {
                // Extraire le numéro de bloc de l'accusé de réception
                int receivedBlockNumber = (unsigned char)buffer[2] << 8 | (unsigned char)buffer[3];
                // Trouver la demande associée à l'adresse du client
                RequestInfo *request = findRequest(addr);
                if (request != NULL && receivedBlockNumber == request->expectedBlockNumber) {
                    // Vérifier si l'ACK correspond au bloc attendu
                    // Mettre à jour expectedBlockNumber
                    // Vérifier si tous les blocs ont été envoyés avec succès
                    printf("[INFO] Received ACK for block %d\n", receivedBlockNumber);
                    readBytes = fread(buffer + 4, 1, 512, request->fp);
                    retries = 0;
                    // Envoyer le bloc suivant au client
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
                    // Si tous les blocs ont été envoyés, fermer le fichier et supprimer la demande
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

        // Réinitialiser master_fds après avoir traité chaque requête
        FD_ZERO(&master_fds);
        FD_SET(server_sockfd, &master_fds);
        max_fd = server_sockfd;
    }

    return 0;
}
