#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <cstdint>
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

struct RDTHeader{
    uint32_t seq_num;
    uint32_t ack_num;
    uint16_t flags;
    uint16_t checksum;
    uint32_t data_len;
};

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
map<string, uint32_t> next_expected_seq;

// Flags para el protocolo
constexpr uint16_t FLAG_SYN = 0x01;    // Sincronización
constexpr uint16_t FLAG_ACK = 0x02;    // Confirmación
constexpr uint16_t FLAG_FIN = 0x04;    // Finalización  
constexpr uint16_t FLAG_DATA = 0x08;   // Datos

// Configuración RDT
constexpr int MAX_RETRIES = 3;
constexpr int TIMEOUT_MS = 2000;


uint16_t calcularChecksum(const char* data, size_t len) {
    uint32_t sum = 0;
    for (size_t i = 0; i < len; ++i) {
        sum += static_cast<uint8_t>(data[i]);
        // Suma simple para debug
    }
    return static_cast<uint16_t>(sum & 0xFFFF);
}

bool verificarChecksum(const char* data, size_t len) {
    if (len < sizeof(RDTHeader)) return false;
    
    // Crear una copia del header para verificar
    RDTHeader header_copia;
    memcpy(&header_copia, data, sizeof(RDTHeader));
    
    // Guardar el checksum recibido
    uint16_t checksum_recibido = header_copia.checksum;
    
    // Crear header temporal con checksum = 0 para cálculo
    header_copia.checksum = 0;
    string datos_temp(reinterpret_cast<char*>(&header_copia), sizeof(RDTHeader));
    if (len > sizeof(RDTHeader)) {
        datos_temp += string(data + sizeof(RDTHeader), len - sizeof(RDTHeader));
    }
    
    uint16_t checksum_calculado = calcularChecksum(datos_temp.data(), datos_temp.size());
    
    cout << "DEBUG Checksum - Recibido: " << checksum_recibido 
         << ", Calculado: " << checksum_calculado 
         << ", Match: " << (checksum_recibido == checksum_calculado) << endl;
    
    return checksum_recibido == checksum_calculado;
}
string construirPaqueteRDT(uint32_t seq_num, uint32_t ack_num, uint16_t flags, const string& datos) {
    RDTHeader header{};
    header.seq_num = seq_num;
    header.ack_num = ack_num;
    header.flags = flags;
    header.data_len = datos.size();
    
    // Primero construir el paquete sin checksum
    string packet_data(reinterpret_cast<char*>(&header), sizeof(header));
    packet_data += datos;
    
    // Calcular checksum sobre TODO el paquete
    header.checksum = calcularChecksum(packet_data.data(), packet_data.size());
    
    cout << "DEBUG: Checksum calculado: " << header.checksum 
         << " para datos de " << packet_data.size() << " bytes" << endl;
    
    // Reconstruir con checksum
    packet_data = string(reinterpret_cast<char*>(&header), sizeof(header));
    packet_data += datos;
    
    return packet_data;
}

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

    char buffer[MAXLINE + sizeof(RDTHeader)];
    while (true) {
        sockaddr_in cliaddr{};
        socklen_t clen = sizeof(cliaddr);
        ssize_t n = recvfrom(sockfd, buffer, sizeof(buffer), 0,
                             reinterpret_cast<sockaddr*>(&cliaddr), &clen);
        if (n <= 0) continue;

        // === PROCESAMIENTO RDT ===
        if (n < static_cast<ssize_t>(sizeof(RDTHeader))) continue;
        
        RDTHeader* header = reinterpret_cast<RDTHeader*>(buffer);
        string endpoint = clavePuntoFinal(cliaddr);

        // Verificar checksum
        if (!verificarChecksum(buffer,n)) {
            cerr << "Checksum inválido de " << endpoint << "\n";
            continue;
        }

        // Enviar ACK inmediatamente
        string ack_packet = construirPaqueteRDT(0, header->seq_num, FLAG_ACK, "");
        sendto(sockfd, ack_packet.data(), ack_packet.size(), 0,
               reinterpret_cast<sockaddr*>(&cliaddr), clen);

        // Extraer datos reales (sin el header RDT)
        string datos_reales;
        if (header->data_len > 0 && n > static_cast<ssize_t>(sizeof(RDTHeader))) {
            datos_reales = string(buffer + sizeof(RDTHeader), header->data_len);
        } else {
            continue; // No hay datos útiles
        }

        if (datos_reales.empty()) continue;
        
        // === CONTINUAR CON LÓGICA EXISTENTE PERO USAR datos_reales ===
        
        char cmd = datos_reales[0];
        auto transIt = transferencias.find(endpoint);
        
        // Manejar transferencias en curso (PERO usando datos_reales)
        if (transIt != transferencias.end() && cmd != 'f') {
            auto destIt = users.find(transIt->second.toNick);
            if (destIt == users.end()) {
                string msg = "Destinatario desconectado.";
                string err = string("E") + longitudFormato(msg.size(), LEN_MEDIUM) + msg;
                // Enviar error de forma confiable
                string err_packet = construirPaqueteRDT(0, 0, FLAG_DATA, err);
                sendto(sockfd, err_packet.data(), err_packet.size(), 0,
                       reinterpret_cast<sockaddr*>(&cliaddr), clen);
                transferencias.erase(transIt);
                continue;
            }

            // Reenviar chunk al destinatario (CON header RDT)
            string chunk_packet = construirPaqueteRDT(0, 0, FLAG_DATA, datos_reales);
            sendto(sockfd, chunk_packet.data(), chunk_packet.size(), 0,
                   reinterpret_cast<sockaddr*>(&destIt->second.addr), destIt->second.len);

            transIt->second.remaining -= datos_reales.size();
            if (transIt->second.remaining == 0) {
                cout << "Transferencia completada: " << transIt->second.senderNick
                     << " -> " << transIt->second.toNick << '\n';
                transferencias.erase(transIt);
            }
            continue;
        }

        // Procesar comandos con datos_reales en lugar de buffer
        switch (cmd) {
        case 'n': {
            cout << "Recibido del cliente ==> " << datos_reales << '\n';
            if (datos_reales.size() < 1 + LEN_SHORT) break;
            if (!isdigit(datos_reales[1]) || !isdigit(datos_reales[2])) break;
            int lenNick = (datos_reales[1] - '0') * 10 + (datos_reales[2] - '0');
            if (lenNick <= 0 || datos_reales.size() < 1 + LEN_SHORT + lenNick) break;
            string nick(datos_reales.data() + 3, lenNick);

            if (users.count(nick)) {
                string errorText = "Ese nickname ya existe.";
                string msg = string("E") + longitudFormato(errorText.size(), LEN_MEDIUM) + errorText;
                // Enviar error con RDT
                string err_packet = construirPaqueteRDT(0, 0, FLAG_DATA, msg);
                sendto(sockfd, err_packet.data(), err_packet.size(), 0,
                       reinterpret_cast<sockaddr*>(&cliaddr), clen);
            } else {
                users[nick] = {cliaddr, clen};
                endpointToNick[endpoint] = nick;
                cout << "[Nuevo usuario conectado: " << nick << "]\n";
                string ack_cmd = string("A") + longitudFormato(0, LEN_SHORT);
                // Enviar ACK del comando con RDT
                string ack_packet = construirPaqueteRDT(0, 0, FLAG_DATA, ack_cmd);
                sendto(sockfd, ack_packet.data(), ack_packet.size(), 0,
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
            // Enviar lista con RDT
            string reply_packet = construirPaqueteRDT(0, 0, FLAG_DATA, reply);
            sendto(sockfd, reply_packet.data(), reply_packet.size(), 0, 
                   reinterpret_cast<sockaddr*>(&cliaddr), clen);
            break;
        }
        case 'd': {
            if (datos_reales.size() < 1 + LEN_SHORT) break;
            int lenNick = (datos_reales[1] - '0') * 10 + (datos_reales[2] - '0');
            if (datos_reales.size() < 1 + LEN_SHORT + lenNick) break;
            string nick(datos_reales.data() + 3, lenNick);
            users.erase(nick);
            endpointToNick.erase(endpoint);
            transferencias.erase(endpoint);
            cout << "Usuario se ha Desconectado: " << nick << '\n';
            break;
        }
        case 'f': {
            auto senderIt = endpointToNick.find(endpoint);
            if (senderIt == endpointToNick.end()) break;
            string sender = senderIt->second;

            int offset = 1;
            if (datos_reales.size() < offset + LEN_SHORT) break;
            int lenTo = stoi(string(datos_reales.data() + offset, LEN_SHORT));
            offset += LEN_SHORT;
            if (datos_reales.size() < offset + lenTo) break;
            string toNick(datos_reales.data() + offset, lenTo);
            offset += lenTo;

            if (datos_reales.size() < offset + LEN_MEDIUM) break;
            int lenFile = stoi(string(datos_reales.data() + offset, LEN_MEDIUM));
            offset += LEN_MEDIUM;
            if (datos_reales.size() < offset + lenFile) break;
            string fileName(datos_reales.data() + offset, lenFile);
            offset += lenFile;

            if (datos_reales.size() < offset + 10) break;
            size_t fileSize = stoul(string(datos_reales.data() + offset, 10));
            offset += 10;
            if (datos_reales.size() < offset + 64) break;
            string hash(datos_reales.data() + offset, 64);

            auto destIt = users.find(toNick);
            if (destIt == users.end()) {
                string msg = "El destinatario no existe.";
                string err = string("E") + longitudFormato(msg.size(), LEN_MEDIUM) + msg;
                string err_packet = construirPaqueteRDT(0, 0, FLAG_DATA, err);
                sendto(sockfd, err_packet.data(), err_packet.size(), 0,
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

            // Enviar header al destinatario CON RDT
            string header_packet = construirPaqueteRDT(0, 0, FLAG_DATA, header);
            sendto(sockfd, header_packet.data(), header_packet.size(), 0,
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