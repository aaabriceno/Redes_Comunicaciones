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

void registerNickname(int sockfd){
    int n;
    char buffer[BUFFER_SIZE];

    struct timeval tv;
    tv.tv_sec = 1;   // 1 segundo de espera
    tv.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
    
    while (1) {
        cout << "Ingresa tu nombre: ";
        getline(cin, global_nickname);
    
        string nickMsg = string("n") + formatLength(global_nickname.size(), 2) + global_nickname;
        write(sockfd, nickMsg.c_str(), nickMsg.size());
    
        n = read(sockfd, buffer, BUFFER_SIZE - 1);
    
        if (n <= 0) {
            cout << "\nUsuario accepted!\n\n";
            break;
        }
    
        buffer[n] = '\0';
        if (buffer[0] == 'E') {
            int len = stoi(string(buffer + 1, 3));
            string msg(buffer + 4, len);
            cout << "\nError msg => " << msg << "\n\n";
        }
    }
    
    struct timeval tv_reset;
    tv_reset.tv_sec = 0;
    tv_reset.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv_reset, sizeof tv_reset);   
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

        if (buffer[0] == 'L') {
            int len = stoi(string(buffer + 1, 2));
            int begin=3, i=0; 
            cout<<endl<<"\n ---- Current Users ----\n";
            while(len > i++){
                int len_nick = stoi(string(buffer + begin, 2));
                string nick(buffer + begin + 2, len_nick);
                begin = begin + 2 + len_nick;
                cout<<" - "<<nick;
                if(global_nickname==nick) cout<<"(you)";
                cout<<endl;
            }
            cout<<endl;
            //cout <<nick << endl;


        } else if (buffer[0] == 'T') {
            int len = stoi(string(buffer + 1, 2));
            string from(buffer + 3, len);
            int len_msg = stoi(string(buffer + 3 + len, 3));
            string msg(buffer + 6 + len, len_msg);
            cout <<"\n\n["<< from<<": "<< msg <<"]\n"<< endl;


        } else if (buffer[0] == 'M') {
            int len = stoi(string(buffer + 1, 2));
            string from(buffer + 3, len);
            int len_msg = stoi(string(buffer + 3 + len, 3));
            string msg(buffer + 6 + len, len_msg);
            cout <<"\n\nGLOBAL MSG ["<< from<<": "<< msg <<"]\n"<< endl; 
            
            
        } else if (buffer[0] == 'X') {
            cout << "\n[Server end chat]\n";
            break;


        } else if(buffer[0]=='E') {
            int len = stoi(string(buffer + 1, 3));
            string msg(buffer + 4, len);
            cout <<"\n\nError msg => "<< msg << "\n\n";
        }

        cout<<"\nOption: "<<flush;
    }
}

int main(void) {
    struct sockaddr_in serverAddr;
    int sockfd;
    int res, n;
    string buf;

    sockfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd == -1) {
        perror("Cannot create socket");
        exit(EXIT_FAILURE);
    }

    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);

    res = inet_pton(AF_INET, SERVER_IP, &serverAddr.sin_addr);
    connect(sockfd, (struct sockaddr*)&serverAddr, sizeof(serverAddr));

    cout << "Connected to server at " << SERVER_IP << ":" << PORT << endl;

    registerNickname(sockfd);

    thread reader(readThread, sockfd);

    int option;
    do {
        cout << "\n===> Menu ===>\n1)Users List\n2)Message User\n3)Broadcast\n4)Quit\nOption:";
        cin >> option;
        cin.ignore(numeric_limits<streamsize>::max(), '\n');
        
        if (option == 4) {
            char msg = 'X';
            n = write(sockfd, &msg, 1);
            break;


        } else if(option == 3){
            cout<<"\nMessage for all users: ";
            getline(cin, buf);
            string msg = string("m") + formatLength(buf.size(),3) + buf;
            n = write(sockfd, msg.c_str(), msg.size());


        } else if(option == 2){
            string send_to;
            cout<<"\nMessagge for: ";
            getline(cin, send_to);
            cout<<"\nType Messagge: ";
            getline(cin, buf);
            string msg = string("t") + formatLength(send_to.size(),2) + send_to 
            + formatLength(buf.size(),3) + buf;
            n = write(sockfd, msg.c_str(), msg.size());


        } else if(option==1){
            char msg = 'l';
            n = write(sockfd, &msg, 1);
        }
        
    } while (true);

    shutdown(sockfd, SHUT_RDWR);
    close(sockfd);

    reader.join();

    return 0;
}
