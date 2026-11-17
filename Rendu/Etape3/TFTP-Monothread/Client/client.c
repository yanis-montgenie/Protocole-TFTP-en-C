/* SAE Réseau (Protocole TFTP) - Etape 3 Version Monothread - Licence 3 Info 2023-2024 */
/* MONTGNIE Yanis m22101878 - FOLLET Milo f22108222 - Groupe 2 */

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#define BUF_SIZE 516
#define MAX_CLIENTS 10

typedef struct Request {
    int sockfd;
    struct sockaddr_in addr;
    FILE *fp;
    int block_number;
    int mode; // 0 pour RRQ, 1 pour WRQ
} Request;

Request requests[MAX_CLIENTS];
int num_requests = 0;

void dieWithError(char *errorMessage) {
    perror(errorMessage);
    exit(EXIT_FAILURE);
}

int receive_ack(int sockfd, int expected_block_number, struct sockaddr_in *addr) {
    char ack_buf[4];
    socklen_t addr_len = sizeof(struct sockaddr_in);

    int n = recvfrom(sockfd, ack_buf, sizeof(ack_buf), 0, (struct sockaddr *)addr, &addr_len);
    if (n < 0) {
        dieWithError("Error receiving ACK");
    }

    if (ack_buf[1] != 4) {
        dieWithError("Invalid ACK packet");
    }

    int block_number = (ack_buf[2] << 8) | ack_buf[3];
    if (block_number != expected_block_number) {
        dieWithError("Unexpected block number in ACK");
    }

    return 0;
}

void handle_rrq(int sockfd, struct sockaddr_in addr, char *filename) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        dieWithError("Error opening file for reading");
    }

    char buf[BUF_SIZE];
    int block_number = 0;
    int retries = 0;

    while (1) {
        size_t n = fread(buf + 4, 1, BUF_SIZE - 4, fp);
        if (n <= 0) {
            break;
        }

        // Affectation de l'opcode et du numéro de bloc dans le buffer
        buf[1] = 3;
        buf[2] = (block_number >> 8) & 0xFF;
        buf[3] = block_number & 0xFF;

        // Envoi du paquet de données
        if (sendto(sockfd, buf, n + 4, 0, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            dieWithError("Error sending data packet");
        }

        // Attendre l'ACK
        retries = 0;
        while (retries < 3) {
            if (receive_ack(sockfd, block_number, &addr) == 0) {
                break;
            }
            retries++;
        }

        if (retries == 3) {
            dieWithError("Error receiving ACK after 3 retries");
        }

        block_number++;
    }

    fclose(fp);
}

void handle_wrq(int sockfd, struct sockaddr_in addr, char *filename) {
    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        dieWithError("Error opening file for writing");
    }

    char buf[BUF_SIZE];
    int block_number = 0;

    while (1) {
        // Réception du paquet de données
        socklen_t addr_len = sizeof(struct sockaddr_in);
        int n = recvfrom(sockfd, buf, BUF_SIZE, 0, (struct sockaddr *)&addr, &addr_len);
        if (n < 0) {
            dieWithError("Error receiving data packet");
        }

        // Vérification de l'opcode et du numéro de bloc
        if (buf[1] != 3) {
            dieWithError("Invalid data packet");
        }
        int received_block_number = (buf[2] << 8) | buf[3];
        if (received_block_number != block_number) {
            dieWithError("Unexpected block number in data packet");
        }

        // Écriture des données dans le fichier
        if (fwrite(buf + 4, 1, n - 4, fp) < 0) {
            dieWithError("Error writing to file");
        }

        // Envoi du paquet ACK
        char ack_buf[4];
        ack_buf[0] = 0;
        ack_buf[1] = 4;
        ack_buf[2] = (block_number >> 8) & 0xFF;
        ack_buf[3] = block_number & 0xFF;
        if (sendto(sockfd, ack_buf, sizeof(ack_buf), 0, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            dieWithError("Error sending ACK packet");
        }

        block_number++;

        if (n < BUF_SIZE) {
            break;
        }
    }

    fclose(fp);
}

void handle_request(int sockfd, struct sockaddr_in addr, char *buf) {
    char filename[256];
    strncpy(filename, buf + 2, sizeof(filename));
    filename[sizeof(filename) - 1] = '\0';

    if (buf[1] == 1) {
        // RRQ
        handle_rrq(sockfd, addr, filename);
    } else if (buf[1] == 2) {
        // WRQ
        handle_wrq(sockfd, addr, filename);
    } else {
        dieWithError("Invalid request type");
    }
}

int main() {
    int sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockfd < 0) {
        dieWithError("Error creating socket");
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(69);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        dieWithError("Error binding socket");
    }

    fd_set master_fds, read_fds;
    FD_ZERO(&master_fds);
    FD_ZERO(&read_fds);
    FD_SET(sockfd, &master_fds);

    while (1) {
        read_fds = master_fds;
        if (select(FD_SETSIZE, &read_fds, NULL, NULL, NULL) < 0) {
            dieWithError("Error with select");
        }

        for (int i = 0; i < FD_SETSIZE; i++) {
            if (FD_ISSET(i, &read_fds)) {
                if (i == sockfd) {
                    // Nouvelle demande
                    char buf[BUF_SIZE];
                    struct sockaddr_in addr;
                    socklen_t addr_len = sizeof(addr);

                    int n = recvfrom(sockfd, buf, BUF_SIZE, 0, (struct sockaddr *)&addr, &addr_len);
                    if (n < 0) {
                        dieWithError("Error receiving request");
                    }

                    if (num_requests < MAX_CLIENTS) {
                        requests[num_requests].sockfd = sockfd;
                        requests[num_requests].addr = addr;
                        requests[num_requests].fp = NULL;
                        requests[num_requests].block_number = 0;
                        requests[num_requests].mode = buf[1] == 1 ? 0 : 1;

                        num_requests++;

                        handle_request(sockfd, addr, buf);
                    } else {
                        // Trop de clients, envoi d'un message d'erreur
                        char error_buf[4];
                        error_buf[0] = 0;
                        error_buf[1] = 5;
                        error_buf[2] = 0;
                        error_buf[3] = 7; // Code d'erreur 7: Disque plein ou dépassement d'allocation

                        if (sendto(sockfd, error_buf, sizeof(error_buf), 0, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
                            dieWithError("Error sending error message");
                        }
                    }
                }
            }
        }
    }

    close(sockfd);

    return 0;
}
