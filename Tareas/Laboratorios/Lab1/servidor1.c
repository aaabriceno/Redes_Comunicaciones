#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    struct sockaddr_in stSockAddr;
    int SocketFD = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    char buffer[256];
    int n;

    if (-1 == SocketFD) {
        perror("can not create socket");
        exit(EXIT_FAILURE);
    }

    memset(&stSockAddr, 0, sizeof(struct sockaddr_in));
    stSockAddr.sin_family = AF_INET;
    stSockAddr.sin_port = htons(1100);
    stSockAddr.sin_addr.s_addr = INADDR_ANY;

    if (-1 == bind(SocketFD, (const struct sockaddr *)&stSockAddr, sizeof(struct sockaddr_in))) {
        perror("error bind failed");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }

    if (-1 == listen(SocketFD, 10)) {
        perror("error listen failed");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }
    
    // Bucle para aceptar múltiples conexiones
    while (1) {
        printf("Esperando una nueva conexión...\n");
        int ConnectFD = accept(SocketFD, NULL, NULL);
        if (0 > ConnectFD) {
            perror("error accept failed");
            continue; // Continuar esperando la siguiente conexión
        }

        printf("Cliente conectado. Escribe 'chau' para terminar la conversación.\n");

        // Bucle de conversación con el cliente actual
        while(1) {
            bzero(buffer, 256);
            n = read(ConnectFD, buffer, 255);
            if (n <= 0) {
                printf("El cliente se ha desconectado o error.\n");
                break; // Romper el bucle de conversación
            }
            printf("Cliente: %s\n", buffer);

            if (strcmp(buffer, "chau") == 0) {
                printf("El cliente ha terminado la conversacion.\n");
                break; // Romper el bucle de conversación
            }

            printf("Tú: ");
            bzero(buffer, 256);
            fgets(buffer, 256, stdin);
            buffer[strcspn(buffer, "\n")] = 0;

            n = write(ConnectFD, buffer, strlen(buffer));
            if (n < 0) {
                perror("ERROR al escribir en el socket");
                break;
            }
        }
        
        close(ConnectFD); // Cerrar el socket para el cliente actual
    }

    close(SocketFD);
    return 0;
}
