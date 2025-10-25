// Client side implementation of UDP client-server model
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#define PORT	 8080 //definicion del puerto
#define MAXLINE 1024 //maximo de lineas

struct hostent *host; //puntero a estructura de tipo hostent

// Driver code
int main() {
	int sockfd; //variable sockfd de tipo int
	char buffer[MAXLINE]; //array de tam 1024
	char *hello = "Hello from client";
	struct sockaddr_in servaddr; //almacena direccion y numero de
    
    host = (struct hostent *)gethostbyname((char *)"127.0.0.1");

	// Creating socket file descriptor
	if ( (sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) {
		perror("socket creation failed");
		exit(EXIT_FAILURE);
	}

	memset(&servaddr, 0, sizeof(servaddr));
	
	// Filling server information
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(PORT);
    servaddr.sin_addr = *((struct in_addr *)host->h_addr);
	
	int n, len;
	
	sendto(sockfd, (const char *)hello, strlen(hello),
		MSG_CONFIRM, (const struct sockaddr *) &servaddr,
			sizeof(servaddr));
			
	printf("Hello message sent.\n");
		
	n = recvfrom(sockfd, (char *)buffer, MAXLINE,
				MSG_WAITALL, (struct sockaddr *) &servaddr,
				&len);
	buffer[n] = '\0';
	printf("Server : %s\n", buffer);

	close(sockfd);
	return 0;
}