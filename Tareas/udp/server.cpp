#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <iostream>
#include <string>
#include <unordered_map>
#include <map>

using namespace std;

constexpr int PORT = 45000;
constexpr size_t MAXLINE = 2048;
constexpr int LEN_SHORT = 2;
constexpr int LEN_MEDIUM = 3;

struct ClientInfo {
    sockaddr_in addr;
    socklen_t len;
};

struct TransferState {
    string senderNick;
    string toNick;
    string fileName;
    size_t remaining;
    string hash;
};

map<string, ClientInfo> users;
unordered_map<string, string> endpointToNick;
unordered_map<string, TransferState> transferencias;

string longitudFormato(size_t len, int width) {
    string s(width, '0');
    for (int i = width - 1; i >= 0 && len > 0; --i) {
        s[i] = char('0' + (len % 10));
        len /= 10;
    }
    return s;
}

string clavePuntoFinal(const sockaddr_in& addr) {
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
    return string(ip) + ":" + to_string(ntohs(addr.sin_port));
}

int main() {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd == -1) { perror("socket"); return EXIT_FAILURE; }

    sockaddr_in servaddr{};
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(PORT);
    servaddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sockfd, reinterpret_cast<sockaddr*>(&servaddr), sizeof(servaddr)) == -1) {
        perror("bind");
        close(sockfd);
        return EXIT_FAILURE;
    }

    char buffer[MAXLINE + 1];
    while (true) {
        sockaddr_in cliaddr{};
        socklen_t clen = sizeof(cliaddr);
        ssize_t n = recvfrom(sockfd, buffer, MAXLINE, 0,
                             reinterpret_cast<sockaddr*>(&cliaddr), &clen);
        if (n <= 0) continue;

        string endpoint = clavePuntoFinal(cliaddr);
        auto transIt = transferencias.find(endpoint);
        if (transIt != transferencias.end() && buffer[0] != 'f') {
            auto destIt = users.find(transIt->second.toNick);
            if (destIt == users.end()) {
                string msg = "Destinatario desconectado.";
                string err = string("E") + longitudFormato(msg.size(), LEN_MEDIUM) + msg;
                sendto(sockfd, err.data(), err.size(), 0,
                       reinterpret_cast<sockaddr*>(&cliaddr), clen);
                transferencias.erase(transIt);
                continue;
            }

            size_t chunk = static_cast<size_t>(n);
            if (chunk > transIt->second.remaining) chunk = transIt->second.remaining;

            sendto(sockfd, buffer, chunk, 0,
                   reinterpret_cast<sockaddr*>(&destIt->second.addr), destIt->second.len);

            transIt->second.remaining -= chunk;
            if (transIt->second.remaining == 0) {
                cout << "Transferencia completada: " << transIt->second.senderNick
                     << " -> " << transIt->second.toNick << '\n';
                transferencias.erase(transIt);
            }
            continue;
        }

        if (n <= 0) continue;
        char cmd = buffer[0];

        switch (cmd) {
        case 'n': {
            cout << "Recibido del cliente ==> " << string(buffer, n) << '\n';
            if (n < 1 + LEN_SHORT) break;
            if (!isdigit(buffer[1]) || !isdigit(buffer[2])) break;
            int lenNick = (buffer[1] - '0') * 10 + (buffer[2] - '0');
            if (lenNick <= 0 || n < 1 + LEN_SHORT + lenNick) break;
            string nick(buffer + 3, lenNick);

            if (users.count(nick)) {
                string errorText = "Ese nickname ya existe.";
                string msg = string("E") + longitudFormato(errorText.size(), LEN_MEDIUM) + errorText;
                sendto(sockfd, msg.data(), msg.size(), 0,
                       reinterpret_cast<sockaddr*>(&cliaddr), clen);
            } else {
                users[nick] = {cliaddr, clen};
                endpointToNick[endpoint] = nick;
                cout << "[Nuevo usuario conectado: " << nick << "]\n";
                string ack = string("A") + longitudFormato(0, LEN_SHORT);
                sendto(sockfd, ack.data(), ack.size(), 0,
                       reinterpret_cast<sockaddr*>(&cliaddr), clen);
            }
            break;
        }
        case 'l': {
            string payload;
            for (const auto& entry : users) {
                payload += entry.first + '\n';
            }
            if (payload.empty()) payload = "(sin usuarios)\n";

            string reply = string("L") + longitudFormato(payload.size(), LEN_MEDIUM) + payload;
            sendto(sockfd, reply.data(), reply.size(), 0, reinterpret_cast<sockaddr*>(&cliaddr), clen);
            break;
        }
        case 'd': {
            if (n < 1 + LEN_SHORT) break;
            int lenNick = (buffer[1] - '0') * 10 + (buffer[2] - '0');
            if (n < 1 + LEN_SHORT + lenNick) break;
            string nick(buffer + 3, lenNick);
            users.erase(nick);
            endpointToNick.erase(endpoint);
            transferencias.erase(endpoint);
            cout << "Desconectado: " << nick << '\n';
            break;
        }
        case 't': {
            if (n < 1 + LEN_SHORT) break;
            int lenTo = stoi(string(buffer + 1, LEN_SHORT));
            if (n < 1 + LEN_SHORT + lenTo) break;
            string toNick(buffer + 1 + LEN_SHORT, lenTo);
            
            int offset = 1 + LEN_SHORT + lenTo;
            if (n < offset + LEN_MEDIUM) break;
            int lenMsg = stoi(string(buffer + offset, LEN_MEDIUM));
            offset += LEN_MEDIUM;
            if (n < offset + lenMsg) break;
            string message(buffer + offset, lenMsg);
            
            string from = endpointToNick[endpoint];
            cout << "Mensaje privado de " << from << " a " << toNick << ": " << message << endl;

            auto destIt = users.find(toNick);
            if (destIt == users.end()) {
                string errorMsg = "El usuario destino no existe.";
                string err = string("E") + longitudFormato(errorMsg.size(), LEN_MEDIUM) + errorMsg;
                sendto(sockfd, err.data(), err.size(), 0,
                    reinterpret_cast<sockaddr*>(&cliaddr), clen);
            } else {
                // Formato: 'T' + [len_from(2)] + [from] + [len_msg(3)] + [mensaje]
                string msgToSend = string("T") + 
                                longitudFormato(from.size(), LEN_SHORT) + from +
                                longitudFormato(message.size(), LEN_MEDIUM) + message;
                sendto(sockfd, msgToSend.data(), msgToSend.size(), 0,
                    reinterpret_cast<sockaddr*>(&destIt->second.addr), destIt->second.len);
                cout << "Mensaje enviado a " << toNick << endl;
            }
            break;
        }
        case 'm': {  // Mensaje a todos (broadcast)
            if (n < 1 + LEN_MEDIUM) break;
            int lenMsg = stoi(string(buffer + 1, LEN_MEDIUM));
            if (n < 1 + LEN_MEDIUM + lenMsg) break;
            string message(buffer + 1 + LEN_MEDIUM, lenMsg);
            
            string from = endpointToNick[endpoint];
            cout << "Mensaje broadcast de " << from << ": " << message << endl;

            // Formato: 'M' + [len_from(2)] + [from] + [len_msg(3)] + [mensaje]
            string broadcastMsg = string("M") + 
                                longitudFormato(from.size(), LEN_SHORT) + from +
                                longitudFormato(message.size(), LEN_MEDIUM) + message;

            // Enviar a todos los usuarios excepto al remitente
            for (const auto& user : users) {
                if (user.first != from) {
                    sendto(sockfd, broadcastMsg.data(), broadcastMsg.size(), 0,
                        reinterpret_cast<const sockaddr*>(&user.second.addr), user.second.len);
                }
            }
            cout << "Mensaje broadcast enviado a " << (users.size() - 1) << " usuarios" << endl;
            break;
        }
        case 'f': {
            auto senderIt = endpointToNick.find(endpoint);
            if (senderIt == endpointToNick.end()) break;
            string sender = senderIt->second;

            int offset = 1;
            if (n < offset + LEN_SHORT) break;
            int lenTo = stoi(string(buffer + offset, LEN_SHORT));
            offset += LEN_SHORT;
            if (n < offset + lenTo) break;
            string toNick(buffer + offset, lenTo);
            offset += lenTo;

            if (n < offset + LEN_MEDIUM) break;
            int lenFile = stoi(string(buffer + offset, LEN_MEDIUM));
            offset += LEN_MEDIUM;
            if (n < offset + lenFile) break;
            string fileName(buffer + offset, lenFile);
            offset += lenFile;

            if (n < offset + 10) break;
            size_t fileSize = stoul(string(buffer + offset, 10));
            offset += 10;
            if (n < offset + 64) break;
            string hash(buffer + offset, 64);

            auto destIt = users.find(toNick);
            if (destIt == users.end()) {
                string msg = "El destinatario no existe.";
                string err = string("E") + longitudFormato(msg.size(), LEN_MEDIUM) + msg;
                sendto(sockfd, err.data(), err.size(), 0,
                       reinterpret_cast<sockaddr*>(&cliaddr), clen);
                break;
            }

            cout << "Cabecera archivo de " << sender << " a " << toNick
                 << " -> " << fileName << " (" << fileSize << " bytes)\n";

            string header = string("F")
                + longitudFormato(sender.size(), LEN_SHORT) + sender
                + longitudFormato(fileName.size(), LEN_MEDIUM) + fileName
                + longitudFormato(fileSize, 10)
                + hash;

            sendto(sockfd, header.data(), header.size(), 0,
                   reinterpret_cast<sockaddr*>(&destIt->second.addr), destIt->second.len);

            transferencias[endpoint] = {sender, toNick, fileName, fileSize, hash};
            break;
        }
        default:
            break;
        }
    }

    close(sockfd);
    return EXIT_SUCCESS;
}