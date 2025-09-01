#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <iostream>
#include <string>
#include <thread>
#include <iomanip>
#include <sstream>

using namespace std;

#define PORT 45000
#define BUFFER_SIZE 256
string global_nickname;

string formatLength(size_t len, int cifras) {
    stringstream ss;
    ss << setw(cifras) << setfill('0') << len;
    return ss.str();
}

void readThread(int clientSock) {
    char buffer[BUFFER_SIZE];
    int n;
    while (true) {
        n = read(clientSock, buffer, BUFFER_SIZE - 1);
        if (n <= 0) {
            cout << "\n[Client disconnected]\n";
            break;
        }
        buffer[n] = '\0';

        cout<<buffer<<endl;
        if (buffer[0] == 'n') {
            int len = stoi(string(buffer + 1, 2));
            string nick(buffer + 3, len);
            global_nickname = nick;
            //cout << "\n<- Nickname recibido: " << nick << endl;
        } else if (buffer[0] == 'm') {
            int len = stoi(string(buffer + 1, 3));
            string msg(buffer + 4, len);
            cout <<endl<< global_nickname<<": "<< msg << endl;
            cout << "You: " << flush;
        } else if (buffer[0] == 'X') {
            cout << "\n[Client end chat]\n";
            break;
        }

    }
}

int main(void) {
    struct sockaddr_in serverAddr;
    int serverSock, clientSock;
    int n;
    string buf, nickname;

    cout << "Enter your Nickname: ";
    getline(cin, nickname);

    serverSock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSock == -1) {
        perror("Cannot create socket");
        exit(EXIT_FAILURE);
    }

    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(serverSock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == -1) {
        perror("Bind failed");
        close(serverSock);
        exit(EXIT_FAILURE);
    }

    if (listen(serverSock, 10) == -1) {
        perror("Listen failed");
        close(serverSock);
        exit(EXIT_FAILURE);
    }

    cout << "Server listening on port " << PORT << "...\n";

    int end_prom=1;
    while (end_prom) {
        clientSock = accept(serverSock, NULL, NULL);
        if (clientSock == -1) {
            perror("Accept failed");
            continue;
        }
        cout << "Client connected!\n";

        string nickMsg = string("n") + formatLength(nickname.size(),2) + nickname;
        write(clientSock, nickMsg.c_str(), nickMsg.size());

        thread reader(readThread, clientSock);

        do {
            cout << "You: ";
            getline(cin, buf);

            if (buf == "chau") {
                char end = 'X';
                write(clientSock, &end, 1);
                end_prom = 0;
                break;
            }

            string msg = string("m") + formatLength(buf.size(),3) + buf;
            n = write(clientSock, msg.c_str(), msg.size());
        } while (true);

        shutdown(clientSock, SHUT_RDWR);
        close(clientSock);
        reader.join();
        cout << "Client disconnected." << endl;
    }

    close(serverSock);
    return 0;
}