#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/time.h>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <csignal>
#include <iostream>
#include <fstream>
#include <limits>
#include <string>
#include <cstdint>

using namespace std;

string globalNickname;
int global_sockfd = -1; 

constexpr const char* SERVER_IP = "127.0.0.1";
constexpr int PORT = 45000;
constexpr size_t MAXLINE = 1024;
constexpr int LEN_SHORT = 2;
constexpr int LEN_MEDIUM = 3;

// === ESTRUCTURAS RDT ===
struct RDTHeader {
    uint32_t seq_num;
    uint32_t ack_num;
    uint16_t flags;
    uint16_t checksum;
    uint32_t data_len;
};

// Flags para el protocolo
constexpr uint16_t FLAG_SYN = 0x01;
constexpr uint16_t FLAG_ACK = 0x02;
constexpr uint16_t FLAG_FIN = 0x04;
constexpr uint16_t FLAG_DATA = 0x08;

// Configuración RDT
constexpr int MAX_RETRIES = 3;
constexpr int TIMEOUT_MS = 2000;

// === FUNCIONES RDT ===
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

bool esperarACK(int sockfd, uint32_t seq_esperado, const sockaddr_in& expected_from) {
    char buffer[sizeof(RDTHeader) + MAXLINE];
    sockaddr_in from;
    socklen_t from_len = sizeof(from);
    
    timeval tv{};
    tv.tv_sec = TIMEOUT_MS / 1000;
    tv.tv_usec = (TIMEOUT_MS % 1000) * 1000;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    while (true) {
        ssize_t n = recvfrom(sockfd, buffer, sizeof(buffer), 0,
                           reinterpret_cast<sockaddr*>(&from), &from_len);
        
        if (n == -1) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                return false; // Timeout
            }
            perror("recvfrom en esperarACK");
            return false;
        }
        
        // Verificar que viene del servidor esperado
        if (from.sin_addr.s_addr != expected_from.sin_addr.s_addr || 
            from.sin_port != expected_from.sin_port) {
            continue; // Ignorar paquetes de otros
        }
        
        if (n < static_cast<ssize_t>(sizeof(RDTHeader))) continue;
        
        RDTHeader* header = reinterpret_cast<RDTHeader*>(buffer);
        
        // Verificar checksum
        string received_data(buffer, n);
        if (!verificarChecksum(received_data.data(),n)) {
            cerr << "Checksum inválido en ACK\n";
            continue;
        }
        
        // Verificar si es ACK del número esperado
        if ((header->flags & FLAG_ACK) && header->ack_num == seq_esperado) {
            return true;
        }
    }
}

bool enviarConfiable(int sockfd, const string& datos, const sockaddr_in& dest_addr) {
    static uint32_t next_seq_num = 1;
    
    for (int intento = 0; intento < MAX_RETRIES; ++intento) {
        uint32_t current_seq = next_seq_num;
        string paquete = construirPaqueteRDT(current_seq, 0, FLAG_DATA, datos);
        
        ssize_t sent = sendto(sockfd, paquete.data(), paquete.size(), 0,
                            reinterpret_cast<const sockaddr*>(&dest_addr), 
                            sizeof(dest_addr));
        
        if (sent == -1) {
            perror("sendto en enviarConfiable");
            return false;
        }
        
        cout << "Enviado paquete seq=" << current_seq << " (intento " << (intento+1) << ")\n";
        
        if (esperarACK(sockfd, current_seq, dest_addr)) {
            next_seq_num++;
            cout << "ACK recibido para seq=" << current_seq << "\n";
            return true;
        }
        
        cerr << "Timeout ACK seq=" << current_seq << ", reintentando...\n";
    }
    
    cerr << "Fallo después de " << MAX_RETRIES << " intentos\n";
    return false;
}

// === FUNCIONES ORIGINALES (modificadas para usar RDT) ===
void showMenu(){
    cout << "\n===> Menú ===>\n"
         << "1) Ver Usuarios\n"
         << "2) Enviar Mensaje a Usuario\n"
         << "3) Mensaje a Todos\n"
         << "4) Enviar Archivo\n"
         << "5) Jugar TTT\n"
         << "6) Enviar Objeto Sala\n"
         << "7) Salir\n"
         << "Opción: " << flush;
}

string calcularSHA256(const string& filename){
    const string cmd = "sha256sum \"" + filename + "\" > temp_hash.txt";
    if (system(cmd.c_str()) != 0) return "";
    ifstream in("temp_hash.txt");
    string hash;
    if (in) in >> hash;
    in.close();
    remove("temp_hash.txt");
    return hash;
}

bool manejarCabeceraArchivo(int sockfd, const char* data, ssize_t len, const sockaddr_in& from) {
    // Extraer datos reales (sin header RDT)
    if (len < sizeof(RDTHeader)) return false;
    RDTHeader* header = reinterpret_cast<RDTHeader*>(const_cast<char*>(data));
    
    if (header->data_len == 0) return false;
    string datos_reales(data + sizeof(RDTHeader), header->data_len);
    
    if (datos_reales.size() < 1 + LEN_SHORT + LEN_MEDIUM + 10 + 64) return false;
    
    // Procesar como antes pero con datos_reales
    int offset = 1;
    int lenFrom = stoi(string(datos_reales.data() + offset, LEN_SHORT));
    offset += LEN_SHORT;
    if (datos_reales.size() < offset + lenFrom) return false;
    string sender(datos_reales.data() + offset, lenFrom);
    offset += lenFrom;

    int lenFile = stoi(string(datos_reales.data() + offset, LEN_MEDIUM));
    offset += LEN_MEDIUM;
    if (datos_reales.size() < offset + lenFile) return false;
    string fileName(datos_reales.data() + offset, lenFile);
    offset += lenFile;

    size_t fileSize = stoul(string(datos_reales.data() + offset, 10));
    offset += 10;
    if (datos_reales.size() < offset + 64) return false;
    string hash(datos_reales.data() + offset, 64);

    string targetNick = globalNickname.empty() ? "desconocido" : globalNickname;
    string outputName = sender + "--" + targetNick + "--" + fileName;
    ofstream out(outputName, ios::binary);
    if (!out) {
        cerr << "Error al crear archivo de salida: " << outputName << '\n';
        return false;
    }

    size_t remaining = fileSize;
    char chunkBuf[MAXLINE + sizeof(RDTHeader)];
    
    // Configurar timeout para recepción de chunks
    timeval tv{};
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    while (remaining > 0) {
        sockaddr_in src{};
        socklen_t slen = sizeof(src);
        ssize_t chunk = recvfrom(sockfd, chunkBuf, sizeof(chunkBuf), 0,
                                 reinterpret_cast<sockaddr*>(&src), &slen);
        if (chunk <= 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                cerr << "Timeout recibiendo bloque de archivo.\n";
            } else {
                perror("recvfrom");
            }
            out.close();
            remove(outputName.c_str());
            return false;
        }
        
        if (src.sin_addr.s_addr != from.sin_addr.s_addr || src.sin_port != from.sin_port) {
            cerr << "Bloque ignorado por venir de origen distinto.\n";
            continue;
        }
        
        // Procesar chunk con header RDT
        if (chunk < static_cast<ssize_t>(sizeof(RDTHeader))) continue;
        RDTHeader* chunk_header = reinterpret_cast<RDTHeader*>(chunkBuf);
        size_t chunk_data_size = chunk_header->data_len;
        
        // Enviar ACK para el chunk
        string ack_chunk = construirPaqueteRDT(0, chunk_header->seq_num, FLAG_ACK, "");
        sendto(sockfd, ack_chunk.data(), ack_chunk.size(), 0,
               reinterpret_cast<sockaddr*>(&src), slen);
        
        if (chunk_data_size > 0 && chunk > static_cast<ssize_t>(sizeof(RDTHeader))) {
            size_t writable = chunk_data_size;
            if (writable > remaining) writable = remaining;
            out.write(chunkBuf + sizeof(RDTHeader), writable);
            remaining -= writable;
        }
    }
    out.close();

    // Restaurar timeout normal
    timeval tv_normal{};
    tv_normal.tv_sec = 2;
    tv_normal.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv_normal, sizeof(tv_normal));

    string computed = calcularSHA256(outputName);
    if (!computed.empty() && computed == hash) {
        cout << "Archivo recibido: " << outputName << " (" << fileSize << " bytes)\n";
    } else {
        cerr << "Advertencia: verificación SHA-256 fallida para " << outputName << '\n';
    }
    return true;
}

string longitudFormato(size_t len, int width){
    string s(width, '0');
    for (int i = width - 1; i >= 0 && len > 0; --i){
        s[i] = char('0' + (len % 10));
        len /= 10;
    }
    return s;
}

void handle_exit(int signum) {
    if (global_sockfd != -1 && !globalNickname.empty()) {
        string bye = string("d") + longitudFormato(globalNickname.size(), 2) + globalNickname;
        sockaddr_in servaddr{};
        servaddr.sin_family = AF_INET;
        servaddr.sin_port = htons(PORT);
        inet_pton(AF_INET, SERVER_IP, &servaddr.sin_addr);
        
        // Enviar desconexión de forma confiable
        enviarConfiable(global_sockfd, bye, servaddr);
    }
    close(global_sockfd);
    exit(signum);
}

bool enviarPacket(int sockfd, const string& payload) {
    sockaddr_in servaddr{};
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(PORT);
    if (inet_pton(AF_INET, SERVER_IP, &servaddr.sin_addr) != 1) {
        perror("inet_pton");
        return false;
    }
    
    return enviarConfiable(sockfd, payload, servaddr);
}

bool recibirPacket(int sockfd, string& out, int timeoutSec = 2) {
    char buffer[MAXLINE + sizeof(RDTHeader)];
    sockaddr_in from{};
    socklen_t flen = sizeof(from);

    timeval tv{};
    tv.tv_sec = timeoutSec;
    tv.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    while (true) {
        ssize_t n = recvfrom(sockfd, buffer, sizeof(buffer) - 1, 0,
                             reinterpret_cast<sockaddr*>(&from), &flen);
        if (n == -1) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                cerr << "Timeout recibido.\n";
            } else {
                perror("recvfrom");
            }
            return false;
        }
        
        // Verificar que viene del servidor
        sockaddr_in expected_servaddr{};
        expected_servaddr.sin_family = AF_INET;
        expected_servaddr.sin_port = htons(PORT);
        inet_pton(AF_INET, SERVER_IP, &expected_servaddr.sin_addr);
        
        if (from.sin_addr.s_addr != expected_servaddr.sin_addr.s_addr || 
            from.sin_port != expected_servaddr.sin_port) {
            continue; // Ignorar paquetes que no son del servidor
        }
        
        if (n > 0) {
            // Procesar header RDT
            if (n < static_cast<ssize_t>(sizeof(RDTHeader))) continue;
            
            RDTHeader* header = reinterpret_cast<RDTHeader*>(buffer);
            
            // Verificar checksum
            if (!verificarChecksum(buffer,n)) {
                cerr << "Checksum inválido en paquete recibido\n";
                continue;
            }
            
            // Enviar ACK
            string ack_packet = construirPaqueteRDT(0, header->seq_num, FLAG_ACK, "");
            sendto(sockfd, ack_packet.data(), ack_packet.size(), 0,
                   reinterpret_cast<sockaddr*>(&from), flen);
            
            // Si es archivo, manejarlo
            if (header->data_len > 0 && n > static_cast<ssize_t>(sizeof(RDTHeader))) {
                string datos_reales(buffer + sizeof(RDTHeader), header->data_len);
                if (!datos_reales.empty() && datos_reales[0] == 'F') {
                    if (manejarCabeceraArchivo(sockfd, buffer, n, from)) {
                        continue; // Continuar esperando más paquetes
                    }
                }
                
                // Si no es archivo, extraer datos normales
                out = datos_reales;
                return true;
            }
        }
    }
}

bool registrarNombre(int sockfd, const string& nickname) {
    string nickMsg = string("n") + longitudFormato(nickname.size(), LEN_SHORT) + nickname;
    cout << "Enviando al servidor ==> " << nickMsg << '\n';
    if (!enviarPacket(sockfd, nickMsg)) return false;

    string reply;
    if (!recibirPacket(sockfd, reply)) return false;
    if (reply.empty()) return false;

    if (reply[0] == 'A') {
        globalNickname = nickname;
        cout << "\nNombre de usuario aceptado!\n\n";
        return true;
    }
    if (reply[0] == 'E' && reply.size() >= 1 + LEN_MEDIUM) {
        int len = stoi(reply.substr(1, LEN_MEDIUM));
        cout << "\nError msg => " << reply.substr(1 + LEN_MEDIUM, len) << "\n\n";
    }
    return false;
}

bool registrarNombreInteractivo(int sockfd) {
    while (true) {
        string nickname;
        cout << "Nickname (o 'q' para salir): ";
        getline(cin, nickname);
        if (nickname.empty()) {
            cerr << "Nickname vacío. Inténtalo de nuevo.\n";
            continue;
        }
        if (nickname == "q" || nickname == "Q") {
            cout << "Saliendo.\n";
            return false;
        }

        if (registrarNombre(sockfd, nickname)) {
            cout << "Registrado como " << nickname << ".\n";
            return true;
        }
    }
}

bool lista_de_Usuarios(int sockfd) {
    const int MAX_ATTEMPTS = 3;
    const int TIMEOUT_SEC = 2;

    string request = "l";

    for (int attempt = 1; attempt <= MAX_ATTEMPTS; ++attempt) {
        cout << "Intentando obtener lista de usuarios (Intento " << attempt << "/" << MAX_ATTEMPTS << ")...\n";
        if (!enviarPacket(sockfd, request)) {
            return false;
        }

        string reply;
        if (recibirPacket(sockfd, reply, TIMEOUT_SEC)) {
            if (!reply.empty() && reply[0] == 'L') {
                size_t last_char = reply.find_last_not_of('#');
                string clean_payload = reply.substr(1, last_char);
                cout << "\n--- Usuarios Conectados ---\n" << clean_payload << "\n--------------------------\n";
                return true;
            } else {
                cerr << "Respuesta inesperada del servidor: " << reply << '\n';
            }
        } else {
            cerr << "Timeout esperando respuesta del servidor.\n";
        }
    }

    cerr << "El servidor no responde después de " << MAX_ATTEMPTS << " intentos.\n";
    return false;
}

bool enviarArchivo(int sockfd){
    string sendTo, filepath;
    cout << "\nEnviar archivo a (nickname):";
    getline (cin, sendTo);
    cout << "Ruta del archivo:";
    getline(cin, filepath);

    ifstream file (filepath, ios::binary);
    if(!file){
        cerr << "Error: No se pudo abrir el archivo.\n";
        return false;
    }

    string hash = calcularSHA256(filepath);
    if( hash.size() != 64){
        cerr << "Error: hash invalido.\n";
        return false;
    }

    file.seekg(0, ios::end);
    size_t fsize = static_cast<size_t>(file.tellg());
    file.seekg(0, ios::beg);

    string filename = filepath;
    size_t slash = filepath.find_last_of("/\\");
    if (slash != string::npos){
        filename = filepath.substr(slash +1);
    }

    string header = string("f")
        + longitudFormato(sendTo.size(), LEN_SHORT) + sendTo
        + longitudFormato(filename.size(), LEN_MEDIUM) + filename
        + longitudFormato(fsize, 10)
        + hash;

    cout << "Enviando al servidor ==> " << header << '\n';
    if (!enviarPacket(sockfd, header)) return false;

    // Enviar contenido del archivo por chunks CON RDT
    sockaddr_in servaddr{};
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(PORT);
    if (inet_pton(AF_INET, SERVER_IP, &servaddr.sin_addr) != 1) {
        perror("inet_pton");
        return false;
    }

    char buffer[1024];
    size_t total = 0;
    while (file) {
        file.read(buffer, sizeof(buffer));
        streamsize chunk = file.gcount();
        if (chunk <= 0) break;
        
        // Enviar chunk de forma confiable
        string chunk_data(buffer, chunk);
        if (!enviarConfiable(sockfd, chunk_data, servaddr)) {
            cerr << "Error enviando chunk del archivo\n";
            return false;
        }
        total += static_cast<size_t>(chunk);
    }

    cout << "[Contenido del archivo enviado: " << total << " bytes]\n";
    return true;
}

int main(){
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd == -1) {
        perror("socket");
        return EXIT_FAILURE;
    }
    global_sockfd = sockfd;
    signal(SIGINT, handle_exit);

    if (!registrarNombreInteractivo(sockfd)) {
        close(sockfd);
        return EXIT_SUCCESS;
    }
    
    bool running = true;
    while (running) {
        showMenu();
        string opt;
        getline(cin, opt);
        if (opt == "1") {
            lista_de_Usuarios(sockfd);
        }
        else if (opt == "2"){
            cout << "Trabajando en ello\n";
        } 
        else if (opt == "3"){
            //
        }
        else if (opt == "4"){
            enviarArchivo(sockfd);
        }
        else if (opt == "5"){

        }
        else if (opt == "6"){
            //
        }
        else if (opt == "7") {
            char msg = 'x';
            cout << "Enviando al servidor ==>" << msg << "\n";
            if (globalNickname.empty()) {
                cerr << "No registrado: no se puede enviar despedida al servidor.\n";
            } else {
                string bye = string("d") + longitudFormato(globalNickname.size(), LEN_SHORT) + globalNickname;
                enviarPacket(sockfd, bye);
            }
            running = false;
        } 
        else {
            cout << "Opción no implementada.\n";
        }
    }

    close(sockfd);
    return EXIT_SUCCESS;
}