/* SAE Réseau (Protocole TFTP) - Etape 3 Version Monothread - Licence 3 Info 2023-2024 */
/* MONTGNIE Yanis m22101878 - FOLLET Milo f22108222 - Groupe 2 */
/* Date de rendu : 18 février 2024 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h> // Ajout de l'en-tête pour la fonction select

#define SIZE 516 // 4 octets d'en-tête + 512 octets de données
#define MAX_CLIENT 10 //nombre max de client en simultané 

void dieWithError(char *errorMessage) {
    perror(errorMessage);
    exit(EXIT_FAILURE);
}

// Structure pour stocker les informations client
typedef struct {
    struct sockaddr_in addr;
    socklen_t addr_size;
} ClientInfo;

// Fonction pour recevoir les ACKs
int receiveACK(int sockfd, int expectedBlockNumber, struct sockaddr_in *addr){
    char ackBuffer[4];
    socklen_t addr_size = sizeof(struct sockaddr_in);

    // Attente de la réception d'un ACK
    int n = recvfrom(sockfd, ackBuffer, sizeof(ackBuffer), 0, (struct sockaddr *)addr, &addr_size);
    
    if (n < 0)
        dieWithError("[ERROR] receiving ACK");

    // Vérifier si le paquet est bien un paquet ACK
    if (ackBuffer[1] != 4)
        dieWithError("[ERROR] Not an ACK packet");
        
    // Extraction du numéro de bloc de l'ACK
    int blockNumber = (unsigned char)ackBuffer[2] << 8 | (unsigned char)ackBuffer[3];

    // Vérifier si le numéro de bloc est celui attendu
    if (blockNumber != expectedBlockNumber)
        dieWithError("[ERROR] Unexpected block number in ACK");

    printf("[INFO] Received ACK for block %d\n", blockNumber);
    return 0;  // ACK reçu et vérifié avec succès
}

// Gestion des requêtes du client
// Gestion des requêtes du client
// Gestion des requêtes du client
void handleRequest(int sockfd, ClientInfo *clients, int *num_clients) {
    char buffer[SIZE];
    FILE *fp = NULL;
    int n;
    printf("heuuu");
    char ackPacket[4]; // ACK packet initialisation
    ackPacket[0] = 0;
    ackPacket[1] = 4;
    ackPacket[2] = 0;
    ackPacket[3] = 0;

    while (1) {
        struct sockaddr_in addr; // Adresse du client
        socklen_t addr_size = sizeof(addr); // Taille de l'adresse du client
        
        fd_set readfds; // Déclaration de l'ensemble de descripteurs de fichiers à surveiller
        FD_ZERO(&readfds); // Initialisation de l'ensemble à vide
        FD_SET(sockfd, &readfds); // Ajout du socket serveur à l'ensemble
        
        // Utilisation de select pour surveiller l'activité sur le socket
        int activity = select(sockfd + 1, &readfds, NULL, NULL, NULL);
        if (activity < 0)
            dieWithError("[ERROR] select error");
        
        if (FD_ISSET(sockfd, &readfds)) { // Vérifie si le socket est prêt à lire
            // Réception du message du client
            n = recvfrom(sockfd, buffer, SIZE, 0, (struct sockaddr *)&addr, &addr_size);
            if (n < 0)
                dieWithError("[ERROR] recvfrom error");

            printf("[INFO] Message received from %s:%d\n", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
            printf("%s", buffer);
            
            // La requête est une requête WRQ (Write Request)
            if (buffer[1] == 2){
                printf("[INFO] Message received from %s:%d\n", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
                char *filename = buffer + 2;  // Extraction du nom du fichier
                
                // Ouverture du fichier en mode écriture pour y écrire les données reçues
                fp = fopen(filename, "wb");
                if (fp == NULL)
                    dieWithError("[ERROR] Cannot open file");
                
                // Envoi d'un premier ACK avec le numéro de bloc à 0 pour confirmer la réception de WRQ
                if (sendto(sockfd, ackPacket, sizeof(ackPacket), 0, (struct sockaddr *)&addr, addr_size) < 0)
                    dieWithError("[ERROR] sendto error");
                
            }
            // Le paquet reçu est un paquet de données
            else if (buffer[1] == 3){
                // Écriture des données reçues dans le fichier
                if (fwrite(buffer + 4, 1, n - 4, fp) < 1)
                    dieWithError("[ERROR] fwrite error");

                // Préparation et envoi d'un ACK pour le bloc reçu
                ackPacket[2] = buffer[2];  // Copie de l'octet de poids fort du numéro de bloc
                ackPacket[3] = buffer[3];  // Copie de l'octet de poids faible du numéro de bloc
                
                // Envoi d'un paquet ACK en réponse à la réception d'un paquet de données
                if (sendto(sockfd, ackPacket, sizeof(ackPacket), 0, (struct sockaddr *)&addr, addr_size) < 0)
                    dieWithError("[ERROR] sendto error");

                // Le nombre d'octets reçu est inférieur à 512, c'est qu'on est arrivé à la fin du fichier
                if (n < 512){
                    printf("[SUCCESS] File received successfully.\n");
                    fclose(fp);
                    break; // Fin de la transmission
                }
            }
            // La requête est une requête RRQ (Read Request)
            else if (buffer[1] == 1){
                printf("[INFO] Message received from %s:%d\n", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
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
                        dieWithError("[ERROR] sendto error");
                        
                    continue;
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
                    n = sendto(sockfd, buffer, readBytes + 4, 0, (struct sockaddr *)&addr, addr_size);
                    printf("[INFO] Message received from %s:%d\n", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
                    if (n < 0)
                        dieWithError("[ERROR] sending block to the server.");
                    
                    // Attente et vérification de l'ACK pour chaque bloc de données
                    receiveACK(sockfd, blockNumber, &addr);
                    
                    memset(buffer, 0, SIZE);  // Nettoyage du buffer pour le prochain bloc
                }
                
                printf("[SUCCESS] File sent successfully.\n");  // L'envoi du fichier est un succès
                fclose(fp);
                break;
            }
        }
    }
}


int main() {
    // Définition de l'adresse IP et du port du serveur
    char *ip = "127.0.0.1";
    int port = 8080;

    // Création d'un socket UDP pour le serveur
    int server_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (server_sockfd < 0)
        dieWithError("[ERROR] socket error");

    // Configuration de l'adresse du serveur
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(ip);

    // Liaison du socket avec l'adresse du serveur
    if (bind(server_sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
        dieWithError("[ERROR] bind error");

    printf("[STARTING] UDP File Server started on %s:%d.\n\n", ip, port);

    ClientInfo clients[MAX_CLIENT]; // Tableau pour stocker les informations client
    int num_clients = 0;    // Nombre de clients actuellement connectés

    // Gestion des demandes de clients
    while (1) {
        handleRequest(server_sockfd, clients, &num_clients);
    }

    return 0;
}
