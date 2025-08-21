/* Servidor en C para Windows (Winsock2) - Corregido para múltiples mensajes */

#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "Ws2_32.lib")

int main(void)
{
    WSADATA wsaData;
    SOCKET SocketFD, ConnectFD;
    struct sockaddr_in stSockAddr;
    char buffer[256];
    int n;

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        fprintf(stderr, "WSAStartup falló. Código de error: %d\n", WSAGetLastError());
        return 1;
    }

    SocketFD = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (INVALID_SOCKET == SocketFD)
    {
        fprintf(stderr, "No se pudo crear el socket. Código de error: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    memset(&stSockAddr, 0, sizeof(struct sockaddr_in));
    stSockAddr.sin_family = AF_INET;
    stSockAddr.sin_port = htons(45000);
    stSockAddr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (SOCKET_ERROR == bind(SocketFD, (const struct sockaddr *)&stSockAddr, sizeof(struct sockaddr_in)))
    {
        fprintf(stderr, "error bind failed. Código de error: %d\n", WSAGetLastError());
        closesocket(SocketFD);
        WSACleanup();
        return 1;
    }

    if (SOCKET_ERROR == listen(SocketFD, 10))
    {
        fprintf(stderr, "error listen failed. Código de error: %d\n", WSAGetLastError());
        closesocket(SocketFD);
        WSACleanup();
        return 1;
    }

    printf("Servidor esperando conexiones...\n");

    for(;;) // Bucle principal para aceptar nuevas conexiones de diferentes clientes
    {
        ConnectFD = accept(SocketFD, NULL, NULL);
        if (INVALID_SOCKET == ConnectFD)
        {
            fprintf(stderr, "error accept failed. Código de error: %d\n", WSAGetLastError());
            closesocket(SocketFD);
            WSACleanup();
            return 1;
        }

        printf("Conexión establecida con un cliente.\n");

        // Bucle interno para recibir múltiples mensajes del MISMO cliente
        while (1) {
            memset(buffer, 0, sizeof(buffer));
            n = recv(ConnectFD, buffer, sizeof(buffer) - 1, 0);

            if (n <= 0) { // Si n es 0, la conexión fue cerrada por el cliente. Si es < 0, hay un error.
                printf("Conexión cerrada por el cliente o error.\n");
                break; // Salir del bucle interno
            }

            printf("ESTE ES TU MENSAJE: [%s]\n", buffer);

            n = send(ConnectFD, "I got your message", 18, 0);
            if (n == SOCKET_ERROR) {
                fprintf(stderr, "ERROR writing to socket. Código de error: %d\n", WSAGetLastError());
                break; // Salir del bucle interno
            }
        }
        
        // Cerrar la conexión actual para volver a aceptar una nueva
        shutdown(ConnectFD, SD_BOTH);
        closesocket(ConnectFD);
    }

    closesocket(SocketFD);
    WSACleanup();
    return 0;
}