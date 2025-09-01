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
#define SERVER_IP "127.0.0.1"
#define BUFFER_SIZE 256


string global_nickname;

string formatLength(size_t len, int cifras) {
    stringstream ss;
    ss << setw(cifras) << setfill('0') << len;
    return ss.str();
}

void readThread(int socketConn) {
    char buffer[BUFFER_SIZE];
    int n;
    while (true) {
        n = read(socketConn, buffer, BUFFER_SIZE - 1);
        if (n <= 0) {
            cout << "\n[Server disconnected]\n";
            break;
        }
        buffer[n] = '\0';

        if (buffer[0] == 'n') {
            int len = stoi(string(buffer + 1, 2));
            string nick(buffer + 3, len);
            global_nickname = nick;
            //cout <<nick << endl;
        } else if (buffer[0] == 'm') {
            int len = stoi(string(buffer + 1, 3));
            string msg(buffer + 4, len);
            cout <<endl<< global_nickname<<": "<< msg << endl;
            cout << "You: " << flush;
        } else if (buffer[0] == 'X') {
            cout << "\n[Server end chat]\n";
            break;
        }

    }
}

int main(void) {
    struct sockaddr_in serverAddr;
    int sockfd;
    int res, n;
    string buf, nickname;

    cout << "Enter your Nickname: ";
    getline(cin, nickname);

    sockfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd == -1) {
        perror("Cannot create socket");
        exit(EXIT_FAILURE);
    }

    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);

    res = inet_pton(AF_INET, SERVER_IP, &serverAddr.sin_addr);
    if (res <= 0) {
        perror("Invalid address");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    if (connect(sockfd, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == -1) {
        perror("Connection failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    cout << "Connected to server at " << SERVER_IP << ":" << PORT << endl;

    thread reader(readThread, sockfd);

    string nickMsg = string("n") + formatLength(nickname.size(),2) + nickname;
    write(sockfd, nickMsg.c_str(), nickMsg.size());

    do {
        cout << "You: ";
        getline(cin, buf);

        if (buf == "chau") {
            char end = 'X';
            write(sockfd, &end, 1);
            break;
        }

        string msg = string("m") + formatLength(buf.size(),3) + buf;
        n = write(sockfd, msg.c_str(), msg.size());

    } while (true);

    shutdown(sockfd, SHUT_RDWR);
    close(sockfd);

    reader.join();

    return 0;
}
