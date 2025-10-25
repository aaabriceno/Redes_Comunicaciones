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

int main()
{
   int sock; //variable sock de tipo int
   //estructura server_addr de tipo sockaddr_in
   struct sockaddr_in server_addr;
   struct hostent *host; //puntero a estructura de tipo hostent
   char send_data[1024]; //array de tam 1024
   int n;
   
   //almacena nombre,direccion IP, alias
   host = (struct hostent *)gethostbyname((char *)"127.0.0.1");

   if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
   {
      perror("socket");
      exit(1);
   }

   server_addr.sin_family = AF_INET;
   server_addr.sin_port = htons(5000);
   server_addr.sin_addr = *((struct in_addr *)host->h_addr);
   bzero(&(server_addr.sin_zero), 8);

   while (1)
   {

      printf("Type Something (q or Q to quit):");
      gets(send_data);

      if ((strcmp(send_data, "q") == 0) || strcmp(send_data, "Q") == 0)
         break;

      else
      n = sendto(sock, send_data, strlen(send_data), 0, (struct sockaddr *)&server_addr, sizeof(struct sockaddr));
   }
}