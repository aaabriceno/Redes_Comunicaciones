// Client side implementation of UDP client-server model
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define PORT    9999
#define MAXLINE 777

unsigned char calcular_checksum_mod6(const char* data, int n){
    int s=0;
    for(int i=0;i<n;i++)
        s += (unsigned char)data[i];
    return (unsigned char)(s % 6);
}

// Driver code
int main() {
        int sockfd;
        char buffer[MAXLINE], buffer_recv[MAXLINE];
        struct sockaddr_in       servaddr;


        // Creating socket file descriptor
        if ( (sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) {
                perror("socket creation failed");
                exit(EXIT_FAILURE);
        }

        memset(&servaddr, 0, sizeof(servaddr));

        // Filling server information
        servaddr.sin_family = AF_INET;
        servaddr.sin_port = htons(PORT);
        if (inet_pton(AF_INET, "127.0.0.1", &servaddr.sin_addr) != 1) {
                perror("inet_pton falló para la dirección del servidor");
                exit(EXIT_FAILURE);
        }

        ssize_t n;
        socklen_t addr_len = sizeof(servaddr);


        char buf[100];
        int seqNum=0;
        char datagram[778];
        while (1){
                for(int i=0; i<MAXLINE+1; i++) datagram[i] = '\0';
                for(int i=0; i<MAXLINE; i++) buffer[i] = '\0';
                printf("\nEscribe algo (q o Q para salir):");
                if (!fgets(buffer, sizeof(buffer), stdin))
                        break;
                buffer[strcspn(buffer, "\n")] = '\0';

                if ((strcmp(buffer, "q") == 0) || strcmp(buffer, "Q") == 0)
                        break;

                char ch;
                printf("¿Enviar con error? (1=Si, 0=No): ");
                scanf(" %c",&ch);
                getchar();

                datagram[0]='D';
                printf(">>%s<<\n",datagram);
                seqNum++;
                sprintf(buf,"%010d",seqNum);
                strncpy(datagram+1,buf,10);
                printf(">>%s<<\n",datagram);

                do{
                        unsigned char checkSum = calcular_checksum_mod6(buffer, strlen(buffer));

                        if(ch=='1')
                                checkSum = (checkSum + 1) % 6;
                        sprintf(buf,"%d:1",checkSum);
                        datagram[11]= buf[0];

                        printf(">>%s<<\n",datagram);
                        strncpy(datagram+12,buffer,strlen(buffer));
                        printf(">>%s<<\n",datagram);
                        for(int p=12 + strlen(buffer); p<778; p++){
                                datagram[p]='#';        
                        }
                        datagram[777]='\0';
                        //datagram[12+strlen(buffer)]='\0';
                        printf(">>%s<<\n",datagram);
                        // printf("AAA:");
                        // scanf("%c",&ch);

                        sendto(sockfd, datagram , MAXLINE,
                                MSG_CONFIRM, (const struct sockaddr *) &servaddr,
                                sizeof(servaddr));
                

                        printf("[SEND] type=D seq=%d checksum=%d bytes=%ld\n",
                                seqNum,datagram[11],strlen(buffer));

                        addr_len = sizeof(servaddr);
                        n = recvfrom(sockfd, buffer_recv, MAXLINE,
                                MSG_WAITALL, (struct sockaddr*)&servaddr, &addr_len);
                        buffer_recv[n] = '\0';

                        printf("[RECV] %s\n", buffer_recv);
                
                        //datagram[11] = checksum_mod6(buffer, strlen(buffer));
                        ch = '0';
                }while(buffer_recv[0]=='N');                                
                //printf("Hello message sent.\n");

                for(int i=0; i<MAXLINE; i++) buffer[i] = '\0';

                addr_len = sizeof(servaddr);
                do{
                        n = recvfrom(sockfd, (char *)buffer, MAXLINE,
                                                MSG_WAITALL, (struct sockaddr *) &servaddr,
                                                &addr_len);
                        buffer[n] = '\0';

                        char type = buffer[0];
                        //if(type!='D') break;

                        char seqstr[11];
                        strncpy(seqstr, buffer + 1, 10);
                        seqstr[10] = '\0';
                        int seq = atoi(seqstr);
                        printf("%zd",n);

                        unsigned char recv_cs = buffer[11];
                        char* payload = buffer + 12;
                        
                        printf(">> %s\n", buffer);
                        while(buffer[n-1]=='#'){
                                buffer[n-1] = '\0';
                                n--;
                        }
                        printf(">> %s\n", buffer);
                        printf("%s\n",payload);
                        int payload_len = n - 12;

                        unsigned char calc_cs = calcular_checksum_mod6(payload, payload_len);
                        sprintf(buf,"%d:1",calc_cs);  

                        printf("[RECV] type=%c seq=%d bytes=%d checksum=%d checksum_calculated=%d\n",
                        type, seq, payload_len,recv_cs,buf[0]);

                        char reply[20];

                        if (recv_cs != buf[0]) {
                                sprintf(reply, "N%010d", seq);
                                sendto(sockfd, reply, strlen(reply), MSG_CONFIRM,
                                        (struct sockaddr*)&servaddr, addr_len);
                                printf("[SEND] NAK\n");
                                printf("%s\n",reply);
                        }
                        else{
                                sprintf(reply, "A%010d", seq);
                                sendto(sockfd, reply, strlen(reply), MSG_CONFIRM,
                                        (struct sockaddr*)&servaddr, addr_len);
                                printf("[SEND] ACK\n");
                                printf("%s\n",reply);
                                break;                              
                        }
                }while (1);

                int last = MAXLINE - 1;
                while(buffer[last]=='#'){
                        buffer[last] = '\0';
                        last--;
                }
        
                printf("Server : %s\n", buffer+12);
                for(int i=0; i<MAXLINE; i++) buffer[i] = '\0';


        }
        close(sockfd);
        return 0;
}
