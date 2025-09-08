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
#include <map>
#include <vector>

using namespace std;


#define PORT 45000
#define BUFFER_SIZE 256
string global_nickname;
map<string, int> current_users;
map<int, string> current_users_ids;


string formatLength(size_t len, int cifras) {
    stringstream ss;
    ss << setw(cifras) << setfill('0') << len;
    return ss.str();
}

void newClientThread(int clientSock) {
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
            //cout<<nick<<endl;
            auto it = current_users.find(nick);
            if (it != current_users.end()){
                string msg = "The nickname already exists.";
                string msg_error = string("E") + formatLength(msg.size(),3) + msg;
                write(clientSock, msg_error.c_str(), msg_error.size());
            }
            else{
                current_users[nick] = clientSock;
                current_users_ids[clientSock] = nick;
            }


        }else if (buffer[0] == 'm') {
            string from = current_users_ids[clientSock];

            int len_msg = stoi(string(buffer + 1, 3));
            string msg(buffer + 4, len_msg);

            for(auto const &u : current_users){
                if (u.first != from){
                    string msg_list = string("M") + formatLength(from.size(),2) + from
                    + formatLength(msg.size(),3) + msg;
                    write(u.second, msg_list.c_str(), msg_list.size());
                }
            }
            

        } else if (buffer[0] == 't') {
            int len = stoi(string(buffer + 1, 2));
            string to_send(buffer + 3, len);

            int len_msg = stoi(string(buffer + 3 + len, 3));
            string msg(buffer + 6 + len, len_msg);

            string from = current_users_ids[clientSock];

            auto it = current_users.find(to_send);
            if (it == current_users.end()){
                string msg = "The user nickname don\'t exist.";
                string msg_error = string("E") + formatLength(msg.size(),3) + msg;
                write(clientSock, msg_error.c_str(), msg_error.size());
            }
            else{
                string msg_to_send = string("T") + formatLength(from.size(),2) + from 
                + formatLength(msg.size(),3) + msg;
                write(current_users[to_send], msg_to_send.c_str(), msg_to_send.size());
            }


        } else if (buffer[0] == 'l') {
            string msg = "";
            for(auto const &u : current_users){
                msg += formatLength(u.first.size(), 2) + u.first;
            }
            string msg_list = string("L") + formatLength(current_users.size(),2) + msg;
            write(clientSock, msg_list.c_str(), msg_list.size());


        } else if (buffer[0] == 'X') {
            // int len = stoi(string(buffer + 1, 2));
            // string nick(buffer + 3, len);

            // auto it = current_users.find(nick);
            // current_users.erase(nick);

            for (auto it = current_users.begin(); it != current_users.end(); ++it) {
                if (it->second == clientSock) {
                    cout << "\n[Removing user: " << it->first <<"]"<< endl;
                    current_users.erase(it);
                    current_users_ids.erase(clientSock);
                    break;
                }
            }
            shutdown(clientSock, SHUT_RDWR);
            close(clientSock);
            break;
        }

    }
}

int main(void) {
    struct sockaddr_in serverAddr;
    int serverSock, clientSock;
    int n,N=10;
    std::vector<std::thread> threads;
    string buf;

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

    while (1) {
        clientSock = accept(serverSock, NULL, NULL);

        threads.emplace_back(newClientThread, clientSock);
    }

    for (auto& t : threads) {
        t.join();
    }

    close(serverSock);
    return 0;
}

