// Server side implementation of UDP client-server model
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define PORT     9999
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
        struct sockaddr_in servaddr, cliaddr;

        // Creating socket file descriptor
        if ( (sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) {
                perror("socket creation failed");
                exit(EXIT_FAILURE);
        }

        memset(&servaddr, 0, sizeof(servaddr));
        memset(&cliaddr, 0, sizeof(cliaddr));

        // Filling server information
        servaddr.sin_family = AF_INET; // IPv4
        servaddr.sin_addr.s_addr = INADDR_ANY;
        servaddr.sin_port = htons(PORT);

        // Bind the socket with the server address
        if ( bind(sockfd, (const struct sockaddr *)&servaddr,
                        sizeof(servaddr)) < 0 )
        {
                perror("bind failed");
                exit(EXIT_FAILURE);
        }

        socklen_t addr_len = sizeof(cliaddr);
        ssize_t n;

        char buf[100];
        int seqNum=0;
        char datagram[778];

        while(1){
                addr_len = sizeof(cliaddr);
                do{
                        n = recvfrom(sockfd, (char *)datagram, MAXLINE,
                                                MSG_WAITALL, (struct sockaddr *) &cliaddr,
                                                &addr_len);
                        datagram[n] = '\0';

                        char type = datagram[0];

                        //if(type!='D') break;

                        char seqstr[11];
                        strncpy(seqstr, datagram + 1, 10);
                        seqstr[10] = '\0';
                        int seq = atoi(seqstr);
                        printf("%zd",n);
                        unsigned char recv_cs = datagram[11];
                        char* payload = datagram + 12;
                        printf(">> %s\n", datagram);
                        //int last = MAXLINE - 1;
                        while(datagram[n-1]=='#'){
                                datagram[n-1] = '\0';
                                n--;
                        }
                        int payload_len = n - 12;
                        //char ch;
                        printf(">> %s\n", datagram);
                        printf("%s\n",payload);
                        //scanf("%c",&ch);

                        unsigned char calc_cs = calcular_checksum_mod6(payload, payload_len);
                        sprintf(buf,"%d:1",calc_cs);                        
                        //printf("%c\n",recv_cs);
                        //printf("%c\n",buf[0]);

                        printf("[RECV] type=%c seq=%d bytes=%d checksum=%d checksum_calculated=%d\n",
                        type, seq, payload_len,recv_cs,buf[0]);

                        char reply[20];

                        if (recv_cs != buf[0]) {
                                sprintf(reply, "N%010d", seq);
                                sendto(sockfd, reply, strlen(reply), MSG_CONFIRM,
                                        (struct sockaddr*)&cliaddr, addr_len);
                                printf("[SEND] NAK\n");
                                printf("%s\n",reply);
                        }
                        else{
                                sprintf(reply, "A%010d", seq);
                                sendto(sockfd, reply, strlen(reply), MSG_CONFIRM,
                                        (struct sockaddr*)&cliaddr, addr_len);
                                printf("[SEND] ACK\n");
                                printf("%s\n",reply);
                                break;                              
                        }
                }while (1);

                printf(">> %s\n", datagram);
                

                printf("Client : %s\n", datagram + 12);
                for(int i=0; i<MAXLINE; i++) buffer[i] = '\0';
                for(int i=0; i<MAXLINE+1; i++) datagram[i] = '\0';

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
                        printf(">>%s<<\n",datagram);

                        sendto(sockfd, datagram , MAXLINE,
                                MSG_CONFIRM, (const struct sockaddr *) &cliaddr,
                                sizeof(cliaddr));
                

                        printf("[SEND] type=D seq=%d checksum=%d bytes=%ld\n",
                                seqNum,datagram[11],strlen(buffer));

                        addr_len = sizeof(cliaddr);
                        n = recvfrom(sockfd, buffer_recv, MAXLINE,
                                MSG_WAITALL, (struct sockaddr*)&cliaddr, &addr_len);
                        buffer_recv[n] = '\0';

                        printf("[RECV] %s\n", buffer_recv);
                
                        //datagram[11] = checksum_mod6(buffer, strlen(buffer));
                        ch='0';
                }while(buffer_recv[0]=='N'); 
                for(int i=0; i<MAXLINE; i++) buffer[i] = '\0';
        }

        return 0;
}
