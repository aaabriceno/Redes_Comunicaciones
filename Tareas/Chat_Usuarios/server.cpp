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
string global_nickname;
map<string, int> current_users;
map<int, string> current_users_ids;


string formatLength(size_t len, int cifras) {
    stringstream ss;
    ss << setw(cifras) << setfill('0') << len;
    return ss.str();
}

int get_len(int clientSock, int n_prot){
    char readed[4];
    read(clientSock, readed, n_prot);
    readed[n_prot] = '\0';
    return atoi(readed);
}

string read_text(int clientSock, int len){
    char readed[255];
    read(clientSock, readed, len);
    readed[len] = '\0';
    return string(readed);
}

void newClientThread(int clientSock) {
    char buffer[2];
    int n;

    while (true) {
        string recieved = "";
        n = read(clientSock, buffer, 1);
        if (n <= 0) {
            cout << "\n[Client disconnected]\n";
            break;
        }

        recieved += buffer;
        buffer[n] = '\0';

        if (buffer[0] == 'n') {
            int len = get_len(clientSock, 2);
            string nick = read_text(clientSock, len);

            cout<<recieved<<formatLength(len,2)<<nick<<endl;

            auto it = current_users.find(nick);
            if (it != current_users.end()){
                string msg = "The nickname already exists.";
                string msg_error = string("E") + formatLength(msg.size(),3) + msg;
                write(clientSock, msg_error.c_str(), msg_error.size());
                cout<<" /nEnviando  al cliente ==> "<<msg_error<<endl;
            }
            else{
                current_users[nick] = clientSock;
                current_users_ids[clientSock] = nick;
            }


        }else if (buffer[0] == 'm') {
            string from = current_users_ids[clientSock];

            int len_msg = get_len(clientSock, 3); 
            string msg = read_text(clientSock, len_msg);

            cout<<recieved<<formatLength(len_msg,3)<<msg<<endl;

            for(auto const &u : current_users){
                if (u.first != from){
                    string msg_list = string("M") + formatLength(from.size(),2) + from
                    + formatLength(msg.size(),3) + msg;
                    write(u.second, msg_list.c_str(), msg_list.size());
                    cout<<" /nEnviando  al cliente ==> "<<msg_list<<endl;
                }
            }
            

        } else if (buffer[0] == 't') {
            int len = get_len(clientSock, 2);
            string to_send = read_text(clientSock, len);

            int len_msg = get_len(clientSock, 3);
            string msg = read_text(clientSock, len_msg);

            string from = current_users_ids[clientSock];

            cout<<recieved<<formatLength(len,2)<<to_send<<formatLength(len_msg,3)<<msg<<endl;

            auto it = current_users.find(to_send);
            if (it == current_users.end()){
                string msg = "The user nickname don\'t exist.";
                string msg_error = string("E") + formatLength(msg.size(),3) + msg;
                write(clientSock, msg_error.c_str(), msg_error.size());
                cout<<" /nEnviando  al cliente ==> "<<msg_error<<endl;
            }
            else{
                string msg_to_send = string("T") + formatLength(from.size(),2) + from 
                + formatLength(msg.size(),3) + msg;
                write(current_users[to_send], msg_to_send.c_str(), msg_to_send.size());
                cout<<" /nEnviando  al cliente ==> "<<msg_to_send<<endl;
            }


        } else if (buffer[0] == 'l') {
            cout<<recieved<<endl;
            string msg = "";
            for(auto const &u : current_users){
                msg += formatLength(u.first.size(), 2) + u.first;
            }
            string msg_list = string("L") + formatLength(current_users.size(),2) + msg;
            write(clientSock, msg_list.c_str(), msg_list.size());
            cout<<" /nEnviando  al cliente ==> "<<msg_list<<endl;


        } else if (buffer[0] == 'x') {
            cout<<recieved<<endl;
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
