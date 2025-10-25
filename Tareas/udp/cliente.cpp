#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/time.h>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <limits>
#include <string>

using namespace std;

string globalNickname;
constexpr const char* SERVER_IP = "127.0.0.1";
constexpr int PORT = 45000;
constexpr size_t MAXLINE = 1024;
constexpr int LEN_SHORT = 2;
constexpr int LEN_MEDIUM = 3;

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

//funciones auxiliares para enviarArchivo
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
    if (len < 1 + LEN_SHORT + LEN_MEDIUM + 10 + 64) return false;
    int offset = 1;
    int lenFrom = stoi(string(data + offset, LEN_SHORT));
    offset += LEN_SHORT;
    if (len < offset + lenFrom) return false;
    string sender(data + offset, lenFrom);
    offset += lenFrom;

    int lenFile = stoi(string(data + offset, LEN_MEDIUM));
    offset += LEN_MEDIUM;
    if (len < offset + lenFile) return false;
    string fileName(data + offset, lenFile);
    offset += lenFile;

    size_t fileSize = stoul(string(data + offset, 10));
    offset += 10;
    if (len < offset + 64) return false;
    string hash(data + offset, 64);

    string targetNick = globalNickname.empty() ? "desconocido" : globalNickname;
    string outputName = sender + "--" + targetNick + "--" + fileName;
    ofstream out(outputName, ios::binary);
    if (!out) {
        cerr << "Error al crear archivo de salida: " << outputName << '\n';
        return false;
    }

    size_t remaining = fileSize;
    char chunkBuf[MAXLINE];
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
        size_t writable = static_cast<size_t>(chunk);
        if (writable > remaining) writable = remaining;
        out.write(chunkBuf, writable);
        remaining -= writable;
    }
    out.close();

    string computed = calcularSHA256(outputName);
    if (!computed.empty() && computed == hash) {
        cout << "Archivo recibido: " << outputName << " (" << fileSize << " bytes)\n";
    } else {
        cerr << "Advertencia: verificación SHA-256 fallida para " << outputName << '\n';
    }
    return true;
}

string longitudFormato (size_t len, int width){
    string s(width, '0');
    for (int i = width - 1;i >= 0 && len > 0; --i){
        s[i] = char('0' + (len % 10));
        len /= 10;
    }
    return s;
}

bool manejarMensajeEntrante(const string& data) {
    if (data.empty()) return false;
    
    char tipo = data[0];
    int offset = 1;
    
    if (tipo == 'T') {  // Mensaje privado
        if (data.size() < offset + LEN_SHORT) return false;
        int lenFrom = stoi(data.substr(offset, LEN_SHORT));
        offset += LEN_SHORT;
        if (data.size() < offset + lenFrom) return false;
        string from = data.substr(offset, lenFrom);
        offset += lenFrom;
        
        if (data.size() < offset + LEN_MEDIUM) return false;
        int lenMsg = stoi(data.substr(offset, LEN_MEDIUM));
        offset += LEN_MEDIUM;
        if (data.size() < offset + lenMsg) return false;
        string message = data.substr(offset, lenMsg);
        
        cout << "\nMENSAJE PRIVADO de [" << from << "]: " << message << "\n" << endl;
        return true;
    }
    else if (tipo == 'M') {  // Mensaje broadcast
        if (data.size() < offset + LEN_SHORT) return false;
        int lenFrom = stoi(data.substr(offset, LEN_SHORT));
        offset += LEN_SHORT;
        if (data.size() < offset + lenFrom) return false;
        string from = data.substr(offset, lenFrom);
        offset += lenFrom;
        
        if (data.size() < offset + LEN_MEDIUM) return false;
        int lenMsg = stoi(data.substr(offset, LEN_MEDIUM));
        offset += LEN_MEDIUM;
        if (data.size() < offset + lenMsg) return false;
        string message = data.substr(offset, lenMsg);
        
        cout << "\n MENSAJE GLOBAL de [" << from << "]: " << message << "\n" << endl;
        return true;
    }
    
    return false;
}

bool enviarPacket(int sockfd, const string& payload) {
    sockaddr_in servaddr{};
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(PORT);
    if (inet_pton(AF_INET, SERVER_IP, &servaddr.sin_addr) != 1) {
        perror("inet_pton");
        return false;
    }
    ssize_t sent = sendto(sockfd, payload.data(), payload.size(), 0,
                          reinterpret_cast<const sockaddr*>(&servaddr),
                          sizeof(servaddr));
    if (sent == -1) {
        perror("sendto");
        return false;
    }
    return true;
}

bool recibirPacket(int sockfd, string& out, int timeoutSec = 2) {
    char buffer[MAXLINE];
    sockaddr_in from{};
    socklen_t flen = sizeof(from);

    timeval tv{};
    tv.tv_sec = timeoutSec;
    tv.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    while (true) {
        ssize_t n = recvfrom(sockfd, buffer, MAXLINE - 1, 0,
                             reinterpret_cast<sockaddr*>(&from), &flen);
        if (n == -1) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                // Timeout normal, no es error
                return false;
            } else {
                perror("recvfrom");
                return false;
            }
        }
        
        buffer[n] = '\0';
        string received_data(buffer, n);
        
        // Manejar diferentes tipos de paquetes
        if (!received_data.empty()) {
            if (received_data[0] == 'F') {
                if (manejarCabeceraArchivo(sockfd, buffer, n, from)) {
                    continue; // Continuar esperando más paquetes
                }
            }
            else if (received_data[0] == 'T' || received_data[0] == 'M') {
                if (manejarMensajeEntrante(received_data)) {
                    continue; // El mensaje ya fue mostrado, continuar
                }
            }
        }
        
        out = received_data;
        return true;
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

        // Intentar registrar
        if (registrarNombre(sockfd, nickname)) {
            cout << "Registrado como " << nickname << ".\n";
            return true;
        }

        // Si falló, permitir reintento (los mensajes de error y timeouts
        // ya son mostrados por las funciones llamadas).
        cout << "No fue posible registrar el nickname. Inténtalo otra vez.\n";
    }
}

bool lista_de_Usuarios(int sockfd) {
    string request = string("l") + longitudFormato(0, LEN_SHORT);
    if (!enviarPacket(sockfd, request)) return false;

    string reply;
    if (!recibirPacket(sockfd, reply)) return false;
    if (reply.empty() || reply[0] != 'L') {
        cerr << "Respuesta inesperada: " << reply << '\n';
        return false;
    }
    if (reply.size() < 1 + LEN_MEDIUM) {
        cerr << "Paquete truncado.\n";
        return false;
    }
    int payloadLen = stoi(reply.substr(1, LEN_MEDIUM));
    string payload = reply.substr(1 + LEN_MEDIUM);
    if (static_cast<int>(payload.size()) < payloadLen) {
        cerr << "Longitud incoherente en lista.\n";
        return false;
    }
    if (static_cast<int>(payload.size()) > payloadLen) {
        payload.resize(payloadLen);
    }
    cout << "\nUsuarios conectados:\n" << payload << '\n';
    return true;
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
    file.seekg(0,ios::end);
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

    file.clear();
    file.seekg(0, ios::beg);

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
        ssize_t sent = sendto(sockfd, buffer, chunk, 0,
                              reinterpret_cast<const sockaddr*>(&servaddr),
                              sizeof(servaddr));
        if (sent != chunk) {
            perror("sendto");
            return false;
        }
        total += static_cast<size_t>(chunk);
    }

    cout << "[Contenido del archivo enviado: " << total << " bytes]\n";
    return true;
}

bool enviarMensajePrivado(int sockfd) {
    string sendTo, message;
    cout << "\nEnviar mensaje a (nickname): ";
    getline(cin, sendTo);
    cout << "Mensaje: ";
    getline(cin, message);

    if (sendTo.empty() || message.empty()) {
        cout << "Error: nickname o mensaje vacío.\n";
        return false;
    }

    // Formato: 't' + [len_to(2)] + [to_nick] + [len_msg(3)] + [mensaje]
    string packet = string("t") + 
                   longitudFormato(sendTo.size(), LEN_SHORT) + sendTo +
                   longitudFormato(message.size(), LEN_MEDIUM) + message;

    cout << "Enviando mensaje privado a " << sendTo << endl;
    return enviarPacket(sockfd, packet);
}

bool enviarMensajeBroadcast(int sockfd) {
    string message;
    cout << "\nMensaje para todos: ";
    getline(cin, message);

    if (message.empty()) {
        cout << "Error: mensaje vacío.\n";
        return false;
    }

    // Formato: 'm' + [len_msg(3)] + [mensaje]
    string packet = string("m") + 
                   longitudFormato(message.size(), LEN_MEDIUM) + message;

    cout << "Enviando mensaje a todos los usuarios" << endl;
    return enviarPacket(sockfd, packet);
}



int main(){
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd == -1) {
        perror("socket");
        return EXIT_FAILURE;
    }

    // Registrar nombre de forma interactiva (maneja reintentos y timeouts)
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
            enviarMensajePrivado(sockfd);
        } 
        else if (opt == "3"){
            enviarMensajePrivado(sockfd);
        }
        else if (opt == "4"){
            enviarArchivo(sockfd);
        }
        else if (opt == "5") {
            cout << "Juego TTT - En desarrollo\n";
        }
        else if (opt == "6") {
            cout << "Objeto Sala - En desarrollo\n";
        }
        else if (opt == "7") {
            cout << "Enviando al servidor\n";
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

       string temp;
        recibirPacket(sockfd, temp, 1); // Timeout corto para ver si hay mensajes
    }

    close(sockfd);
    return EXIT_SUCCESS;
}