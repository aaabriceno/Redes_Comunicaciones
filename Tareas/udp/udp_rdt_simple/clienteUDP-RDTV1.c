// Client side implementation of UDP client-server model
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#define PORT    9999
#define MAXLINE 1024

// Driver code
int main() {
        int sockfd;
                char buffer[MAXLINE];
        struct hostent *host;
        char *hello = "Hello from client";
        struct sockaddr_in       servaddr;

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


char buf[100];
int seqNum=0;
char datagram[778];
int checkSum=0;
while (1)
   {

        printf("\nType Something (q or Q to quit):");
        gets(buffer);

        if ((strcmp(buffer, "q") == 0) || strcmp(buffer, "Q") == 0)
        break;

        datagram[0]='D';
        printf(">>%s<<\n",datagram);
        seqNum++;
        sprintf(buf,"%010d\0",seqNum);
        strncpy(datagram+1,buf,10);
        printf(">>%s<<\n",datagram);
        checkSum=6;
        sprintf(buf,"%d:1\0",checkSum);
        datagram[11]=buf[0];
        printf(">>%s<<\n",datagram);
        strncpy(datagram+12,buffer,strlen(buffer));
        printf(">>%s<<\n",datagram);
        for(int p=12 + strlen(buffer) + 1; p<778; p++){
                datagram[p]='H';        
                }
        datagram[778]='\0';
        printf(">>%s<<\n",datagram);

        sendto(sockfd, datagram , strlen(datagram),
                MSG_CONFIRM, (const struct sockaddr *) &servaddr,
                        sizeof(servaddr));
        printf("Hello message sent.\n");

        n = recvfrom(sockfd, (char *)buffer, MAXLINE,
                                MSG_WAITALL, (struct sockaddr *) &servaddr,
                                &len);
        buffer[n] = '\0';
        printf("Server : %s\n", buffer);

}
        close(sockfd);
        return 0;
}

