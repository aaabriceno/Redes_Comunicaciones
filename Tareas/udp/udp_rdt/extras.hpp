#ifndef EXTRAS_HPP
#define EXTRAS_HPP

#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <cstdint>
#include <sstream>
#include <fstream>
#include <vector>

#include <unistd.h>        // read, write, close
#include <sys/types.h>     // tipos de socket
#include <sys/socket.h>    // funciones de socket
#include <netinet/in.h>    // sockaddr_in
#include <arpa/inet.h>     // inet_addr, htons, etc.
#include <unordered_map>
#define MAXLINE 777

constexpr uint8_t RDT_TYPE_DATA = 0;
constexpr uint8_t RDT_TYPE_ACK = 1;

using namespace std;
// ================= UDP ========================

struct FilePart;

struct RDTHeader
{
    uint8_t type;
    uint8_t seq;
    uint16_t length;
    uint16_t checksum;
};

struct RDTState
{
    uint8_t sendSeq = 0; //siguiente secuencia que emitiremos
    uint8_t expectedSeq = 0; // secuencia que esperamos recibir
    uint8_t lastAck = 1; // ultimo ack emitido (para duplicados)
};






// Estructura simplificada de un socket UDP
typedef struct {
    int sockfd;
    struct sockaddr_in addr;
} UDP_Socket;

UDP_Socket udp_crear_socket(int puerto, const char *ip, int isServer);

uint16_t rdt_checksum(const RDTHeader& hdr, const vector<char>& payload);
bool rdt_send(UDP_Socket *udp, 
            const sockaddr_in* dest, 
            const vector<char>& payload,
            RDTState& state, 
            uint32_t timeoutMs = 500, 
            unsigned maxRetries = 5, 
            bool verbose = false);

bool rdt_recv(UDP_Socket* udp, 
            vector <char>& payload, 
            sockaddr_in* sender, 
            RDTState& state,
            uint32_t timeoutMs = 0);

void udp_enviar(UDP_Socket* udp, const vector<char>& data, const sockaddr_in* dest, bool verbose=true);
int udp_recibir(UDP_Socket *udp, vector<char>& buffer, struct sockaddr_in *sender);
void udp_close(UDP_Socket *udp);

// ================= UTILIDADES =================
std::string formatLength(int num, int width);
int obtener_longitud(string& buffer, int n_prot);
std::string read_text(string& buffer, int len);
void sendError(UDP_Socket* udp, sockaddr_in* sender, std::string msg_);

// ================= ESTRUCTURAS =================

inline void writeInt(vector<char>& buf, int value) {
    uint32_t net = htonl(value);
    buf.insert(buf.end(), (char*)&net, (char*)&net + 4);
}

inline int readInt(const vector<char>& buf, size_t& off) {
    uint32_t net;
    memcpy(&net, buf.data() + off, 4);
    off += 4;
    return ntohl(net);
}

inline void writeFloat(vector<char>& buf, float value) {
    uint32_t net;
    memcpy(&net, &value, 4);
    net = htonl(net);
    buf.insert(buf.end(), (char*)&net, (char*)&net + 4);
}

inline float readFloat(const vector<char>& buf, size_t& off) {
    uint32_t net;
    memcpy(&net, buf.data() + off, 4);
    off += 4;
    net = ntohl(net);
    float val;
    memcpy(&val, &net, 4);
    return val;
}

inline void writeBool(vector<char>& buf, bool v) {
    buf.push_back(v ? 1 : 0);
}

inline bool readBool(const vector<char>& buf, size_t& off) {
    return buf[off++] != 0;
}

inline void writeString(vector<char>& buf, const string& s) {
    writeInt(buf, s.size());
    buf.insert(buf.end(), s.begin(), s.end());
}

inline string readString(const vector<char>& buf, size_t& off) {
    int len = readInt(buf, off);
    string s(buf.data() + off, len);
    off += len;
    return s;
}

struct Cuaderno {
int paginas;
bool cuadriculado;
char color[16];
};

struct Libro {
std::string titulo;
int anio;
};

struct Regla {
float longitud;
bool metalica;
};

struct Lapiz {
char tipo;    // 'H','B'...
int cantidad;
};

struct Refrigerio {
std::string nombre;
float calorias;
bool saludable;
};

struct Mochila {
std::string propietario;
Cuaderno cuaderno;
Libro libro;
Regla regla;
Lapiz lapiz;
Refrigerio refri;
};

// ================= mochila =================
std::vector<char> serializeString(const std::string& s);
std::string deserializeString(const std::vector<char>& buffer, size_t& offset);
std::vector<char> serializeMochila(const Mochila& m);
Mochila deserializeMochila(const std::vector<char>& buffer);
void sendMochila(UDP_Socket *udp, const string &propietario, const sockaddr_in &serverAddr);
void printMochila(const Mochila& m);

// ================= Files =================
struct FilePart {
    std::string fname;
    std::vector<std::string> chunks;
    size_t total_size = 0;
    size_t received_bytes = 0;
};

void receiveMochila(string& buffer);
void sendFile(UDP_Socket *udp, const string propietario, const sockaddr_in &serverAddr);
enum class FileReceiveEvent {
    Fragment,
    Completed
};

FileReceiveEvent receiveFile(std::string buffer);

#endif
