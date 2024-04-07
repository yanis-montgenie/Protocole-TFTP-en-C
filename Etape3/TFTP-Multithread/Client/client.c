#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <errno.h>


#define SIZE 516 // Taille maximale d'un paquet TFTP (4 octets d'en-tête + 512 octets de données)
#define TIMEOUT 5 // Timeout en secondes
#define MAX_RETRIES 3 // Nombre maximum de tentatives de retransmission


// Fonction pour afficher les erreurs système et quitter le programme
void dieWithError(char *errorMessage) {
    perror(errorMessage);
    exit(EXIT_FAILURE);
}


// Fonction pour recevoir les ACKs avec gestion du timeout
int receiveServerResponse(int sockfd, int expectedBlockNumber, struct sockaddr_in *addr) {
    char ackBuffer[SIZE];
    socklen_t addr_size = sizeof(struct sockaddr_in);
    struct timeval timeout;

    // Définition du timeout pour la socket
    timeout.tv_sec = TIMEOUT;
    timeout.tv_usec = 0;
    
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout)) < 0) {
        dieWithError("[ERROR] setsockopt() failed");
    }

    // Attente de la réception d'un ACK
    int n = recvfrom(sockfd, ackBuffer, sizeof(ackBuffer), 0, (struct sockaddr *) addr, &addr_size);
    
    if (n < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            printf("[ERROR] Timeout occurred while waiting for ACK.\n");
            return -1;  // Indique un timeout pour permettre une retransmission
        }
        else {
            dieWithError("[ERROR] receiving ACK");
        }
    }

    // Vérifier si le paquet est bien un paquet ACK
    if (ackBuffer[1] != 4) {
        if (ackBuffer[1] == 5) printf("[ERROR] Received error packet from server: %s\n", ackBuffer + 4);
        else printf("[ERROR] Undefined error\n");
        return -1;
    }

    // Extraction du numéro de bloc de l'ACK
    int blockNumber = (unsigned char) ackBuffer[2] << 8 | (unsigned char) ackBuffer[3];

    // Vérifier si le numéro de bloc est celui attendu
    if (blockNumber != expectedBlockNumber)
        dieWithError("[ERROR] Unexpected block number in ACK");

    printf("[INFO] Received ACK for block %d\n", blockNumber);
    return 0;  // ACK reçu et vérifié avec succès
}


// Fonction pour envoyer une requête WRQ (Write Request)
void send_WRQ(const char *fileName, int sockfd, struct sockaddr_in addr) {
    char buffer[SIZE];
    int n, readBytes, blockNumber = 0, retries;
    
    // Ouverture du fichier en mode lecture pour y lire les données à envoyer
    FILE *fp = fopen(fileName, "rb");
    
    if (fp == NULL) {
        dieWithError("[ERROR] Could not open file for reading");
    }

    // Construction et envoi d'une requête WRQ
    memset(buffer, 0, SIZE);  // Nettoyage du buffer
    buffer[1] = 2;  // Assignation de l'Opcode d'une WRQ (Opcode pour WRQ est 2) au deuxième octet du buffer
    strcpy(buffer + 2, fileName);  // Copie du nom du fichier
    strcpy(buffer + 2 + strlen(fileName) + 1, "octet");  // Copie du mode ("octet")

    // Calcul de la longueur du paquet WRQ
    int packetLength = 2 + strlen(fileName) + 1 + strlen("octet") + 1;

    // Envoi de la requête WRQ
    n = sendto(sockfd, buffer, packetLength, 0, (struct sockaddr *) &addr, sizeof(addr));
    
    if (n < 0) {
        fclose(fp);
        dieWithError("[ERROR] sending WRQ to the server.");
    }

    // Attente et vérification de l'ACK pour la requête WRQ
    if (receiveServerResponse(sockfd, 0, &addr) != 0) {  // Gestion de la retransmission pour WRQ
        fclose(fp);
        exit(EXIT_FAILURE);
    }

    // Préparation du buffer pour l'envoi du fichier
    memset(buffer, 0, SIZE);  // Nettoyage du buffer avant l'envoi des données

    // Envoi du fichier par blocs de 512 octets
    while ((readBytes = fread(buffer + 4, 1, 512, fp)) > 0) {
        blockNumber++;
        retries = 0;
        
        while (retries < MAX_RETRIES) {
            printf("[SENDING] Block %d\n", blockNumber);
            buffer[1] = 3;  // Opcode pour DATA
            buffer[2] = (blockNumber >> 8) & 0xFF;  // Octet de poids fort du numéro de bloc
            buffer[3] = blockNumber & 0xFF;  // Octet de poids faible du numéro de bloc

            // Envoi du bloc de données
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

            // Attente et vérification de l'ACK pour chaque bloc de données
            if (receiveServerResponse(sockfd, blockNumber, &addr) == 0)
                break;  // ACK reçu, continuer avec le prochain bloc
            
            else {
                if (retries == MAX_RETRIES) {
                    fclose(fp);
                    dieWithError("[ERROR] No ACK received for block after max retries.");
                }
                printf("[RETRY] No ACK for block, retrying...\n");
                retries++;
            }
        }
        memset(buffer, 0, SIZE);  // Nettoyage du buffer pour le prochain bloc
    }

    printf("[SUCCESS] File sent successfully.\n");  // L'envoi du fichier est un succès
    fclose(fp);
}



// Fonction pour envoyer une requête RRQ (Read Request)
void send_RRQ(const char *fileName, int sockfd, struct sockaddr_in addr) {
    socklen_t addr_size = sizeof(struct sockaddr_in);
    addr_size = sizeof(addr);
    char buffer[SIZE];
    int n, retries;
    
    // Ouverture du fichier en mode écriture pour y écrire les données reçues
    FILE *fp = fopen(fileName, "w+");
    
    if (fp == NULL)
        dieWithError("[ERROR] Could not open file for writing");

    // Configuration du timeout pour la socket
    struct timeval tv;
    tv.tv_sec = TIMEOUT;
    tv.tv_usec = 0;
    
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv) < 0)
        dieWithError("[ERROR] setsockopt failed");

    // Construction et envoi d'une requête RRQ
    memset(buffer, 0, SIZE);  // Nettoyage du buffer
    buffer[1] = 1; // Assignation de l'Opcode d'une RRQ (Opcode pour RRQ est 1) au deuxième octet du buffer
    strcpy(buffer + 2, fileName);  // Copie du nom du fichier
    strcpy(buffer + 2 + strlen(fileName) + 1, "octet");  // Copie du mode ("octet")

    // Calculer la longueur du paquet RRQ
    int packetLength = 2 + strlen(fileName) + 1 + strlen("octet") + 1;

    // Envoi de la requête RRQ
    n = sendto(sockfd, buffer, packetLength, 0, (struct sockaddr *) &addr, sizeof(addr));
    if (n < 0)
        dieWithError("[ERROR] sending RRQ to the server.");

    // Préparation du buffer pour l'envoi du fichier
    memset(buffer, 0, SIZE);  // Nettoyage du buffer avant l'envoi des données

    char ackPacket[4]; // ACK packet initialisation
    ackPacket[0] = 0;
    ackPacket[1] = 4;
    ackPacket[2] = 0;
    ackPacket[3] = 0;
    
    // Boucle pour recevoir les données du serveur et écrire dans le fichier
    while (1) {
        retries = 0;
        
        while (retries < MAX_RETRIES) {
            // Réception des données du serveur
            n = recvfrom(sockfd, buffer, SIZE, 0, (struct sockaddr *) &addr, &addr_size);
            
            // Gestion des erreurs de réception
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
            
            // Vérification si le paquet reçu est un paquet DATA
            if (buffer[1] == 3) {
                // Écriture des données reçues dans le fichier
                if (fwrite(buffer + 4, 1, n - 4, fp) < 1)
                    dieWithError("[ERROR] fwrite error");

                // Préparation et envoi d'un ACK pour le bloc reçu
                ackPacket[2] = buffer[2];  // Copie de l'octet de poids fort du numéro de bloc
                ackPacket[3] = buffer[3];  // Copie de l'octet de poids faible du numéro de bloc
                
                if (sendto(sockfd, ackPacket, sizeof(ackPacket), 0, (struct sockaddr *) &addr, addr_size) < 0)
                    dieWithError("[ERROR] sendto error");

                break; // Sortie de la boucle de retransmission
            }
            // Gestion des erreurs
            else if (buffer[1]==5){
                printf("[ERROR] Received error packet from server: %s\n", buffer + 4);
                fclose(fp);
                exit(EXIT_FAILURE);
            }
        }

        // Le nombre d'octets reçu est inférieur à 512, c'est qu'on est arriver à la fin du fichier
        if (n < 512) {
            if (retries == MAX_RETRIES){
                printf("[ERROR] Max retries attempt. File cannot be received");
            }
            else {
                printf("[SUCCESS] File received successfully.\n");
            }
            
            fclose(fp);
            break;  // Fin de la transmission
        }
    }
}


int main(int argc, char* argv[]) {
    char ip[15], filename[256], request[4];
    int port;
    
    // Définition de l'adresse IP et du port du serveur TFTP
    
    strcpy(ip,"127.0.0.1");
    port = 8080;
    
    // Vérification des arguments de la ligne de commande
    if (argc < 3) {
    	printf("[ERROR] Invalid arguments\n");
    	exit(EXIT_FAILURE);
    }
    
    // Récupération de la demande de l'utilisateur et du nom de fichier depuis les arguments de la ligne de commande
    strcpy(request, argv[1]);
    strcpy(filename, argv[2]);

    // Création de la socket UDP
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in server_addr;
    
    if (sockfd < 0)
        dieWithError("[ERROR] socket error");

    // Configuration de l'adresse du serveur
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(ip);

    // Envoi de la requête WRQ ou RRQ en fonction de la demande de l'utilisateur
    if (strcmp("put", request) == 0) {
        send_WRQ(filename, sockfd, server_addr);
    } 
    else if (strcmp("get", request) == 0) {
        send_RRQ(filename, sockfd, server_addr);
    } 
    else {
        dieWithError("[ERROR] Invalid request type");
    }

    // Fermeture de la socket
    close(sockfd);
    return 0;
}
