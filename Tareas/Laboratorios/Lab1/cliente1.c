#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    struct sockaddr_in stSockAddr;
    int Res;
    int SocketFD = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    ssize_t n;
    char buffer[256];

    if (-1 == SocketFD) {
        perror("cannot create socket");
        exit(EXIT_FAILURE);
    }

    memset(&stSockAddr, 0, sizeof(struct sockaddr_in));
    stSockAddr.sin_family = AF_INET;
    stSockAddr.sin_port = htons(1100);
    Res = inet_pton(AF_INET, "127.0.0.1", &stSockAddr.sin_addr);

    if (Res <= 0) {
        perror("inet_pton failed");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }

    if (-1 == connect(SocketFD, (const struct sockaddr *)&stSockAddr, sizeof(struct sockaddr_in))) {
        perror("connect failed");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }

    printf("Conectado al servidor. Escribe 'chau' para salir.\n");

    while(1) {
        printf("Tú: ");
        bzero(buffer, 256);
        fgets(buffer, sizeof(buffer), stdin);
        buffer[strcspn(buffer, "\n")] = 0;

        n = write(SocketFD, buffer, strlen(buffer));
        if (n < 0) {
            perror("ERROR al escribir en el socket");
            break;
        }

        if (strcmp(buffer, "chau") == 0) {
            printf("Desconectando...\n");
            break; // Salir del bucle y cerrar la conexión
        }

        bzero(buffer, 256);
        n = read(SocketFD, buffer, 255);
        if (n <= 0) {
            printf("El servidor se ha desconectado o error.\n");
            break;
        }
        printf("Servidor: %s\n", buffer);
    }
    
    shutdown(SocketFD, SHUT_RDWR);
    close(SocketFD);
    return 0;
}
