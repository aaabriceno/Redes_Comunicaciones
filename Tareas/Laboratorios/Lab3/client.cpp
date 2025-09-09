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

string global_nickname;

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



void registerNickname(int sockfd){
    int n;
    char buffer[2];

    struct timeval tv;
    tv.tv_sec = 1;   // 1 segundo de espera
    tv.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
    
    while (1) {
        cout << "Enter your Nickname: ";
        getline(cin, global_nickname);
    
        string nickMsg = string("n") + formatLength(global_nickname.size(), 2) + global_nickname;
        write(sockfd, nickMsg.c_str(), nickMsg.size());
        cout<<" /nEnviando  al servidor ==> "<<nickMsg<<endl;
    
        n = read(sockfd, buffer, 1);
    
        if (n <= 0) {
            cout << "\nNickname accepted!\n\n";
            break;
        }

        cout<<buffer<<endl;
    
        buffer[n] = '\0';
        if (buffer[0] == 'E') {
            int len = get_len(sockfd, 3);
            string msg = read_text(sockfd, len);
            cout<<buffer<<formatLength(len,3)<<msg<<endl;
            cout << "\nError msg => " << msg << "\n\n";
        }
    }
    
    struct timeval tv_reset;
    tv_reset.tv_sec = 0;
    tv_reset.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv_reset, sizeof tv_reset);   
}

void readThread(int socketConn) {
    char buffer[2];
    int n;
    while (true) {
        string recived = "";
        n = read(socketConn, buffer, 1);
        if (n <= 0) {
            cout << "\n[Server disconnected]\n";
            break;
        }
        recived += buffer;
        buffer[n] = '\0';

        if (buffer[0] == 'L') {
            int len = get_len(socketConn, 2);
            recived += formatLength(len,2);
            cout<<endl<<"\n ---- Current Users ----\n";
            for(int i=0; i<len; i++){
                int len_nick = get_len(socketConn, 2);
                string nick = read_text(socketConn, len_nick);
                recived += formatLength(len_nick,2) + nick;
                cout<<" - "<<nick;
                if(global_nickname==nick) cout<<"(you)";
                cout<<endl;
            }
            cout<<endl;
            
            cout<<recived<<endl;

        } else if (buffer[0] == 'T' || buffer[0] == 'M') {
            int len = get_len(socketConn, 2);
            string from = read_text(socketConn, len);
            int len_msg = get_len(socketConn, 3);
            string msg = read_text(socketConn, len_msg);

            cout<<recived<<formatLength(len,2)<<from<<formatLength(len_msg,3)<<msg<<endl;

            cout<<"\n\n";
            if (buffer[0] == 'M') cout<<"GLOBAL MSG ";
            cout <<"["<< from<<": "<< msg <<"]\n"<< endl;


        } else if(buffer[0]=='E') {
            int len = get_len(socketConn, 3);
            string msg = read_text(socketConn, len);
            cout<<recived<<formatLength(len,3)<<msg<<endl;
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
            char msg = 'x';
            n = write(sockfd, &msg, 1);
            cout<<" /nEnviando  al servidor ==> "<<msg<<endl;
            break;


        } else if(option == 3){
            cout<<"\nMessage for all users: ";
            getline(cin, buf);
            string msg = string("m") + formatLength(buf.size(),3) + buf;
            n = write(sockfd, msg.c_str(), msg.size());
            cout<<" /nEnviando  al servidor ==> "<<msg<<endl;


        } else if(option == 2){
            string send_to;
            cout<<"\nMessagge for: ";
            getline(cin, send_to);
            cout<<"\nType Messagge: ";
            getline(cin, buf);
            string msg = string("t") + formatLength(send_to.size(),2) + send_to 
            + formatLength(buf.size(),3) + buf;
            n = write(sockfd, msg.c_str(), msg.size());
            cout<<" /nEnviando  al servidor ==> "<<msg<<endl;


        } else if(option==1){
            char msg = 'l';
            n = write(sockfd, &msg, 1);
            cout<<" /nEnviando  al servidor ==> "<<msg<<endl;
        }
        
    } while (true);

    shutdown(sockfd, SHUT_RDWR);
    close(sockfd);

    reader.join();

    return 0;
}
