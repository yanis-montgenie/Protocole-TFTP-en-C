/* SAE Réseau (Protocole TFTP) - Etape 3 Version Multithread - Licence 3 Info 2023-2024 */
/* MONTGNIE Yanis m22101878 - FOLLET Milo f22108222 - Groupe 2 */
/* Date de rendu : 18 février 2024 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <pthread.h>

#define SIZE 516 // 4 octets d'en-tête + 512 octets de données
#define TIMEOUT 5 // Timeout en secondes
#define MAX_RETRIES 5 // Nombre maximum de tentatives de retransmission
#define MAX_CLIENT 15 // Nombre maximum de clients

pthread_t threads[MAX_CLIENT]; // Tableau d'identifiants de threads
int num_clients = 0;           // Nombre de threads clients créés
pthread_mutex_t client_mutex = PTHREAD_MUTEX_INITIALIZER; // Mutex pour gérer l'accès à 'num_clients'
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;   // Mutex pour gérer l'accès aux fichiers


// Structure pour passer les arguments au thread de traitement du client
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


// Fonction permettant l'envoi d'un paquet d'erreur TFTP
void sendErrorPacket(int sockfd, struct sockaddr_in *addr, int errorCode, const char *errorMessage) {
    char errorBuffer[SIZE];
    int errorMessageLength = strlen(errorMessage) + 1;
    errorBuffer[0] = 0;
    errorBuffer[1] = 5;  // Assignation de l'Opcode d'erreur au deuxième octet paquet
    errorBuffer[2] = (errorCode >> 8) & 0xFF;
    errorBuffer[3] = errorCode & 0xFF;  // Assignation du code d'erreur au troisième et quatrième octet du paquet
    strcpy(errorBuffer + 4, errorMessage); // Message d'erreur

    sendto(sockfd, errorBuffer, 4 + errorMessageLength, 0, (struct sockaddr *)addr, sizeof(struct sockaddr_in));  // Envoi du paquet d'erreur
}


// Fonction pour recevoir les ACKs
int receiveACK(int sockfd, int expectedBlockNumber, struct sockaddr_in *addr){
    char ackBuffer[4];
    socklen_t addr_size = sizeof(struct sockaddr_in);

    // Attente de la réception d'un ACK
    int n = recvfrom(sockfd, ackBuffer, sizeof(ackBuffer), 0, (struct sockaddr *)addr, &addr_size);
    
    if (n < 0)
        printf("[ERROR] receiving ACK");

    // Vérifier si le paquet est bien un paquet ACK
    if (ackBuffer[1] != 4)
        printf("[ERROR] Not an ACK packet");
        
    // Extraction du numéro de bloc de l'ACK
    int blockNumber = (unsigned char)ackBuffer[2] << 8 | (unsigned char)ackBuffer[3];

    // Vérifier si le numéro de bloc est celui attendu
    if (blockNumber != expectedBlockNumber)
        printf("[ERROR] Unexpected block number in ACK");

    printf("[INFO] Received ACK for block %d\n", blockNumber);
    return 0;  // ACK reçu et vérifié avec succès
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
    char ackPacket[4] = {0, 4, 0, 0}; // Initialisation du paquet ACK
    char *filename;

    memcpy(buffer, clientArgs->data, len);
    free(args);

    while (running) {
        
        // La requête est une requête WRQ (Write Request)
        if (buffer[1] == 2){
            char *filename = buffer + 2;  // Extraction du nom du fichier
            
            if (access(filename, F_OK) != -1) {
                perror("[ERROR] File already exists");
                
                buffer[1] = 5;  // Assignation de l'Opcode d'erreur (Opcode pour les erreurs est 5) au deuxième octet du buffer
                buffer[3] = 6;  // Assignation du code d'erreur (code d'erreur pour un fichier non trouvé) au troisème octet du buffer
                strcpy(buffer + 4, "File already exists");  // Assignation du message d'erreur
                
                if (sendto(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr *)&addr, addr_size) < 0)
                    printf("[ERROR] sendto error");
                
                continue;
            }
            
            // Ouverture du fichier en mode écriture pour y écrire les données reçues
            fp = fopen(filename, "wb");
            if (fp == NULL) {
                // Envoyer un paquet d'erreur indiquant que le fichier ne peut pas être créé
                sendErrorPacket(sockfd, &addr, 2, "Cannot create file.");
                running = 0;
                break;
            }
            
            // Envoi d'un premier ACK avec le numéro de bloc à 0 pour confirmer la réception de WRQ
            if (sendto(sockfd, ackPacket, sizeof(ackPacket), 0, (struct sockaddr *)&addr, addr_size) < 0) {
                printf("[ERROR] sendto error\n");
                running = 0;
            }
            break;
        }
        // Le paquet reçu est un paquet de données
        else if (buffer[1] == 3){
            // Écriture des données reçues dans le fichier
            if (fwrite(buffer + 4, 1, n - 4, fp) < 1){
                printf("[ERROR] fwrite error\n");
                running = 0;
                break;
            }
            
            // Préparation et envoi d'un ACK pour le bloc reçu
            ackPacket[2] = buffer[2];  // Copie de l'octet de poids fort du numéro de bloc
            ackPacket[3] = buffer[3];  // Copie de l'octet de poids faible du numéro de bloc
            
            // Envoi d'un paquet ACK en réponse à la réception d'un paquet de données
            if (sendto(sockfd, ackPacket, sizeof(ackPacket), 0, (struct sockaddr *)&addr, addr_size) < 0) {
                printf("[ERROR] sendto error\n");
                running = 0;
                break;
            }
            
            // Le nombre d'octets reçu est inférieur à 512, c'est qu'on est arriver à la fin du fichier
            if (n < 512){
                printf("[SUCCESS] File received successfully.\n");
                fclose(fp);
                running = 0;
                break; // Fin de la transmission
            }
            break;
        }
        // La requête est une requête RRQ (Read Request)
        else if (buffer[1] == 1){
            int readBytes, blockNumber = 0;
            char *filename = buffer + 2;
            
            // Ouverture du fichier en mode lecture pour y lire les données à envoyer
            fp = fopen(filename, "r");
            if (fp == NULL){
                perror("[ERROR] Cannot open file");  // Le fichier n'existe pas dans le serveur
                
                buffer[1] = 5;  // Assignation de l'Opcode d'erreur (Opcode pour les erreurs est 5) au deuxième octet du buffer
                buffer[3] = 1;  // Assignation du code d'erreur (code d'erreur pour un fichier non trouvé) au troisème octet du buffer
                strcpy(buffer + 4, "File not found");  // Assignation du message d'erreur
                
                if (sendto(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr *)&addr, addr_size) < 0)
                    printf("[ERROR] sendto error");
                    
                running = 0;
                break;
            }
            
            // Préparation du buffer pour l'envoi du fichier
            memset(buffer, 0, SIZE);  // Nettoyage du buffer
            
            // Envoi du fichier par blocs de 512 octets
            while ((readBytes = fread(buffer + 4, 1, 512, fp)) > 0){
                blockNumber++;
                printf("[SENDING] Block %d\n", blockNumber);
                buffer[1] = 3;  // Assignation de l'Opcode DATA (Opcode pour les datas est 3) au deuxième octet du buffer
                
                // Définir le numéro de bloc
                buffer[2] = (blockNumber >> 8) & 0xFF; // Octet de poids fort du numéro de bloc
                buffer[3] = blockNumber & 0xFF;        // Octet de poids faible du numéro de bloc
                
                // Envoi du bloc de données
                n = sendto(sockfd, buffer, readBytes + 4, 0, (struct sockaddr *)&addr, sizeof(addr));
                
                if (n < 0)
                    printf("[ERROR] sending block to the server.");
                
                // Attente et vérification de l'ACK pour chaque bloc de données
                receiveACK(sockfd, blockNumber, &addr);
                
                memset(buffer, 0, SIZE);  // Nettoyage du buffer pour le prochain bloc
            }
            
            printf("[SUCCESS] File sent successfully.\n");  // L'envoi du fichier est un succès
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
    // Définition de l'adresse IP et du port du serveur
    char *ip = "127.0.0.1";
    int port = 8080;

    // Création d'un socket UDP pour le serveur
    int server_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (server_sockfd < 0) {
        perror("[ERROR] socket error");
        exit(EXIT_FAILURE);
    }

    // Configuration de l'adresse du serveur
    struct sockaddr_in server_addr, client_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(ip);

    // Liaison du socket avec l'adresse du serveur
    int r = bind(server_sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    
    if (r < 0) dieWithError("[ERROR] Bind error", server_sockfd);

    char buffer[SIZE];
    socklen_t addr_len = sizeof(client_addr);
    
    printf("[STARTING] UDP File Server started on %s:%d.\n", ip, port);
    
    while(1) {
        sleep(1); // Donne la priorité aux autres recvfrom des threads clients
        printf("[INFO] Waiting for requests...\n");
        
        r = recvfrom(server_sockfd, buffer, SIZE, 0, (struct sockaddr *)&client_addr, &addr_len);
        if (r < 0) continue;

        if (buffer[1] == 1 || buffer[1] == 2) { // Création d'un thread si la requête est une RRQ ou une WRQ
            // Création et affectation de la structure pour les arguments du thread client
            ClientArgs* clientArgs = (ClientArgs*)malloc(sizeof(ClientArgs));
            if (!clientArgs) continue;

            clientArgs->sockfd = server_sockfd;
            clientArgs->addr = client_addr;
            memcpy(clientArgs->data, buffer, r);
            clientArgs->len = r;

            // Création d'un nouveau thread pour gérer la requête
            pthread_mutex_lock(&client_mutex);
            if (num_clients < MAX_CLIENT) {
                if (pthread_create(&threads[num_clients++], NULL, handleRequest, (void*)clientArgs) != 0) {
                    perror("[ERROR] Failed to create thread");
                    free(clientArgs); // Libérez la mémoire si la création du thread échoue
                }
            } else {
                printf("[INFO] Maximum number of clients reached. Dropping request.\n");
                free(clientArgs); // Libérez la mémoire si le nombre maximal de clients est atteint
            }
            pthread_mutex_unlock(&client_mutex);
        }
        
    }

    printf("[CLOSING] Closing the server.\n");
    // Fermeture du socket du serveur
    close(server_sockfd);

    return 0;
}

