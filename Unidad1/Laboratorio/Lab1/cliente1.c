#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
    struct sockaddr_in stSockAddr;
    int Res;
    int SocketFD = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    ssize_t n;
    
    char buffer[256];
    char respuestaServidor[256];

    if (-1 == SocketFD) {
        perror("cannot create socket");
        exit(EXIT_FAILURE);
    }

    memset(&stSockAddr, 0, sizeof(struct sockaddr_in));

    stSockAddr.sin_family = AF_INET;
    stSockAddr.sin_port = htons(1100);
    // IMPORTANTE: Usar "127.0.0.1" si el servidor está en la misma máquina
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

    do {
        
        printf("Escribe el mensaje: ");
        fgets(buffer, sizeof(buffer), stdin);
        buffer[strcspn(buffer, "\n")] = 0;

        n = write(SocketFD, buffer, strlen(buffer));
        if (n < 0) {
            perror("ERROR al escribir en el socket");
            break; 
        }

        if (strcmp(buffer, "chau") != 0) {
            bzero(respuestaServidor, 256);
            n = read(SocketFD, respuestaServidor, 255);
            if (n < 0) {
                perror("ERROR al leer del socket");
                break; // Salir si hay error
            }
            printf("Respuesta del servidor: [%s]\n", respuestaServidor);
        }

    } while (strcmp(buffer, "chau") != 0);

    printf("Desconectando...\n");
    
    shutdown(SocketFD, SHUT_RDWR);
    close(SocketFD);

    return 0;
}
