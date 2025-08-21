/* Código cliente en C para Windows (Winsock2) con bucle do-while */

#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "Ws2_32.lib")

int main(void)
{
    WSADATA wsaData;
    SOCKET SocketFD;
    struct sockaddr_in stSockAddr;
    int Res;
    int n;
    
    char mensaje[256];
    char respuestaServidor[256];

    // 1. Inicializar Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        fprintf(stderr, "WSAStartup falló. Código de error: %d\n", WSAGetLastError());
        return 1;
    }

    // 2. Crear el socket
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
    
    Res = inet_pton(AF_INET, "172.16.164.132", &stSockAddr.sin_addr);
    if (Res <= 0) 
    {
        fprintf(stderr, "Error en inet_pton. Código de error: %d\n", WSAGetLastError());
        closesocket(SocketFD);
        WSACleanup();
        return 1;
    }

    // 3. Conectar al servidor
    if (SOCKET_ERROR == connect(SocketFD, (const struct sockaddr *)&stSockAddr, sizeof(struct sockaddr_in)))
    {
        fprintf(stderr, "Conexión fallida. Código de error: %d\n", WSAGetLastError());
        closesocket(SocketFD);
        WSACleanup();
        return 1;
    }
    
    // 4. Bucle para enviar y recibir mensajes
    do {
        printf("Escribe el mensaje (o 'chau' para salir): ");
        fgets(mensaje, sizeof(mensaje), stdin);
        
        // Eliminar el salto de línea que añade fgets
        mensaje[strcspn(mensaje, "\n")] = 0;

        // Si el mensaje es "chau", no enviar y salir del bucle
        if (strcmp(mensaje, "chau") == 0) {
            break;
        }

        // Enviar el mensaje
        n = send(SocketFD, mensaje, strlen(mensaje), 0);
        if (n == SOCKET_ERROR) {
            fprintf(stderr, "Error al enviar datos. Código de error: %d\n", WSAGetLastError());
            break;
        }

        // Limpiar buffer y recibir la respuesta del servidor
        memset(respuestaServidor, 0, sizeof(respuestaServidor));
        n = recv(SocketFD, respuestaServidor, sizeof(respuestaServidor) - 1, 0);
        if (n > 0) {
            printf("Respuesta del servidor: [%s]\n", respuestaServidor);
        } else if (n == 0) {
            printf("Conexión cerrada por el servidor.\n");
            break;
        } else {
            fprintf(stderr, "Error al recibir datos. Código de error: %d\n", WSAGetLastError());
            break;
        }

    } while (1);

    // 5. Cerrar la conexión
    shutdown(SocketFD, SD_BOTH);
    closesocket(SocketFD);
    WSACleanup();

    return 0;
}