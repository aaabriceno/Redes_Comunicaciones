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

    if(-1 == SocketFD) {
        perror("can not create socket");
        exit(EXIT_FAILURE);
    }

    memset(&stSockAddr, 0, sizeof(struct sockaddr_in));
    stSockAddr.sin_family = AF_INET;
    stSockAddr.sin_port = htons(1100);
    stSockAddr.sin_addr.s_addr = INADDR_ANY;

    if(-1 == bind(SocketFD,(const struct sockaddr *)&stSockAddr, sizeof(struct sockaddr_in))) {
        perror("error bind failed");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }

    if(-1 == listen(SocketFD, 10)) {
        perror("error listen failed");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }

    for(;;) {
        printf("Esperando una nueva conexión...\n");
        int ConnectFD = accept(SocketFD, NULL, NULL);
        if(0 > ConnectFD) {
            perror("error accept failed");
            continue; 
        }
        
        printf("Cliente conectado. Esperando mensajes...\n");
        while((n = read(ConnectFD, buffer, 255)) > 0) {
            buffer[n] = '\0';
            printf("Mensaje recibido: [%s]\n", buffer);

            n = write(ConnectFD, "Mensaje recibido", 16);
            if (n < 0) {
                perror("ERROR al escribir en el socket");
                break;
            }
        }
        
        if (n == 0) {
            printf("Cliente se ha desconectado.\n");
        } else if (n < 0) {
            perror("ERROR al leer del socket");
        }

        close(ConnectFD);
    }
    close(SocketFD);
    return 0;
}
