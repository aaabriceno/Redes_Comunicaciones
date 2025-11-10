#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <cstdint>
#include <sstream>
#include <fstream>
#include <chrono>
#include <unordered_map>

#include <unistd.h>        // read, write, close, sendto, recvfrom
#include <sys/types.h>     // tipos de socket
#include <sys/socket.h>    // funciones de socket
#include <netinet/in.h>    // sockaddr_in
#include <arpa/inet.h>     // inet_addr, htons, etc.    
#include <sys/select.h>
#include "extras.hpp"

using namespace std;
unordered_map<string, FilePart> udp_active_files;


// ============> UDP :
UDP_Socket udp_crear_socket(int port, const char *ip, int isServer) {
    UDP_Socket udp;
    udp.sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp.sockfd < 0) {
        perror("Error al crear socket UDP");
        exit(EXIT_FAILURE);
    }

    memset(&udp.addr, 0, sizeof(udp.addr));
    udp.addr.sin_family = AF_INET;
    udp.addr.sin_port = htons(port);
    udp.addr.sin_addr.s_addr = ip ? inet_addr(ip) : INADDR_ANY;

    if (isServer) {
        if (bind(udp.sockfd, (const struct sockaddr *)&udp.addr, sizeof(udp.addr)) < 0) {
            perror("Error al hacer bind");
            close(udp.sockfd);
            exit(EXIT_FAILURE);
        }
    }

    return udp;
}


void udp_enviar(UDP_Socket* udp, const vector<char>& data, const sockaddr_in* dest, bool verbose) {
    vector<char> msg = data;
    if (msg.size() < MAXLINE) {
        msg.insert(msg.end(), MAXLINE - msg.size(), '#');
    }
    
    if (verbose){
        for(int i=0;i<MAXLINE;i++) cout<<msg[i];
        cout<<endl;
        cout<<string(data.begin(), data.end())<<endl;
    }

    sendto(udp->sockfd, msg.data(), msg.size(), 0, (const sockaddr*)dest, sizeof(*dest));
}

int udp_recibir(UDP_Socket* udp, vector<char>& buffer, sockaddr_in* sender) {
    buffer.assign(MAXLINE, 0);
    socklen_t len = sizeof(*sender);
    int n = recvfrom(udp->sockfd, buffer.data(), MAXLINE, 0, (sockaddr*)sender, &len);
    if (n <= 0) return -1;
    /*
    if (buffer[0]!='f' && buffer[0]!='F'){
        for(int i=0;i<MAXLINE;i++) cout<<buffer[i];
        cout<<endl;
    }*/

    while (!buffer.empty() && buffer.back() == '#') buffer.pop_back();
    return buffer.size();
}


void udp_close(UDP_Socket *udp) {
    close(udp->sockfd);
}

// =========== tcp :

string formatLength(int num, int width) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%0*d", width, num);
    return string(buf);
}

// string formatLength(size_t len, int cifras) {
//     stringstream ss;
//     ss << setw(cifras) << setfill('0') << len;
//     return ss.str();
// }

int obtener_longitud(string& buffer, int n_prot){
    string readed = buffer.substr(0,n_prot);
    buffer.erase(0,n_prot);
    return stoi(readed);
}

string read_text(string& buffer, int len){
    string s = buffer.substr(0,len);
    buffer.erase(0,len);
    return s;
}

void sendError(UDP_Socket* udp, sockaddr_in* sender, string msg_){
    string msg_error = string("E") + formatLength(msg_.size(),3) + msg_;
    //cout<<" /nEnviando  al cliente ==> "<<msg_error<<endl;
    vector<char> msg(msg_error.begin(), msg_error.end());
    udp_enviar(udp, msg, sender);
}


namespace {
    string addr_key(const sockaddr_in& addr){
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr.sin_addr, ip, INET_ADDRSTRLEN); //convierte una ip addr de formato bin a texot legible
        return string(ip) + ":" + to_string(ntohs(addr.sin_port));
    }

    void write_header(vector<char>& packet, const RDTHeader& hdr){
        packet.resize(sizeof(RDTHeader));
        packet[0] = hdr.type;
        packet[1] = hdr.seq;
    uint16_t lenNet = htons(hdr.length);
    uint16_t chkNet = htons(hdr.checksum);
    memcpy(packet.data() + 2, &lenNet, sizeof(lenNet));
    memcpy(packet.data() + 4, &chkNet, sizeof(chkNet));
    }

    bool parse_header(const vector<char>& packet, RDTHeader & hdr){
        if (packet.size() < sizeof(RDTHeader)){
            return false;
        }

        hdr.type  = static_cast<uint8_t>(packet[0]);
        hdr.seq = static_cast<uint8_t>(packet[1]);
        memcpy(&hdr.length, packet.data() + 2, sizeof(uint16_t));
        memcpy(&hdr.checksum,packet.data() + 4, sizeof(uint16_t));
        hdr.length = ntohs(hdr.length);
        hdr.checksum = ntohs(hdr.checksum);
        return packet.size() >= sizeof(RDTHeader) + hdr.length;
    }

    void send_ack(UDP_Socket* udp,const sockaddr_in* dest, uint8_t seq){
        RDTHeader hdr{};
        hdr.type = RDT_TYPE_ACK;
        hdr.seq = seq;
        hdr.length = 0;
    hdr.checksum = rdt_checksum(hdr, std::vector<char>{});
        vector<char> pkt;
        write_header(pkt, hdr);
        sendto(udp->sockfd, pkt.data(), pkt.size(),0, reinterpret_cast<const sockaddr*>(dest),sizeof(*dest));
    }
}

uint16_t rdt_checksum(const RDTHeader& hdr, const vector<char>& payload){
    uint32_t sum = 0;
    auto add_word = [&](uint16_t val) {
        sum += val;
        if (sum > 0xFFFF){
            sum = (sum & 0xFFFF) + (sum >> 16);
        }
    };
    
    add_word(static_cast<uint16_t>(hdr.type));
    add_word(static_cast<uint16_t>(hdr.seq));
    add_word(htons(hdr.length));
    add_word(0); //checsum field se considera 0 durante el calculo

    size_t i = 0;
    while (i + 1 < payload.size()){
        uint16_t word = (static_cast<uint8_t>(payload[i]) << 8) | static_cast<uint8_t>(payload[i+1]);
        add_word(word);
        i += 2;
    }
    if (i < payload.size()){
        uint16_t last = static_cast<uint8_t>(payload[i]) << 8;
        add_word(last);
    }

    return static_cast<uint16_t>(~sum);
}

bool rdt_send(UDP_Socket* udp,
            const sockaddr_in* dest,
            const vector<char>& payload,
            RDTState& state,
            uint32_t timeoutMs,
            unsigned maxRetries,
            bool verbose){
    RDTHeader hdr {};
    hdr.type = RDT_TYPE_DATA;
    hdr.seq = state.sendSeq;
    hdr.length = static_cast<uint16_t>(payload.size());
    hdr.checksum = 0;
    hdr.checksum = rdt_checksum(hdr, payload);

    vector <char> packet;
    write_header(packet,hdr);
    packet.insert(packet.end(), payload.begin(), payload.end());

    for (unsigned attempt = 0; attempt < maxRetries; ++attempt){
        if(verbose){
            cout << "[RDT] send seq=" << int(hdr.seq)
                 << " len=" << hdr.length
                 << " try=" << attempt + 1 << endl;
        }

        ssize_t sent = sendto(udp->sockfd, packet.data(), packet.size(), 0,
                            reinterpret_cast<const sockaddr*> (dest),
                            sizeof(*dest));
        
        if (sent < 0){
            perror("[RDT] sendto");
            return false;
        }

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(udp->sockfd, &readfds);

        timeval tv{};
        tv.tv_sec  = timeoutMs / 1000;
        tv.tv_usec = (timeoutMs % 1000) * 1000;

        int ready = select(udp->sockfd + 1, &readfds, nullptr, nullptr,
                           timeoutMs ? &tv : nullptr);

        if (ready <= 0) {
            // timeout o error -> reintentar
            continue;
        }

        vector<char> ackBuf(sizeof(RDTHeader) + 16);
        sockaddr_in ackAddr;
        socklen_t addrLen = sizeof(ackAddr);
        ssize_t rec = recvfrom(udp->sockfd, ackBuf.data(), ackBuf.size(), 0,
                               reinterpret_cast<sockaddr*>(&ackAddr), &addrLen);
        if (rec < static_cast<ssize_t>(sizeof(RDTHeader))) {
            continue;
        }

        ackBuf.resize(static_cast<size_t>(rec));
        RDTHeader ackHdr{};
        if (!parse_header(ackBuf, ackHdr)) continue;
        if (ackHdr.type != RDT_TYPE_ACK || ackHdr.seq != state.sendSeq) continue;

        // ack válido recibido
        state.sendSeq ^= 1;
        state.lastAck = ackHdr.seq;
        return true;
    }

    cerr << "[RDT] exceeded retries for seq "
              << int(state.sendSeq) << endl;
    return false;
} 


bool rdt_recv(UDP_Socket* udp,
              vector<char>& payload,
              sockaddr_in* sender,
              RDTState& state,
              uint32_t timeoutMs) {

    fd_set readfds;
    fd_set master;
    FD_ZERO(&master);
    FD_SET(udp->sockfd, &master);

    while (true) {
        readfds = master;
        timeval tv{};
        timeval* tvPtr = nullptr;
        if (timeoutMs) {
            tv.tv_sec  = timeoutMs / 1000;
            tv.tv_usec = (timeoutMs % 1000) * 1000;
            tvPtr = &tv;
        }

        int ready = select(udp->sockfd + 1, &readfds, nullptr, nullptr, tvPtr);
        if (ready <= 0) {
            return false; // timeout o error
        }

        vector<char> buffer(MAXLINE);
        socklen_t len = sizeof(*sender);
        ssize_t rec = recvfrom(udp->sockfd, buffer.data(), buffer.size(), 0,
                               reinterpret_cast<sockaddr*>(sender), &len);
        if (rec < static_cast<ssize_t>(sizeof(RDTHeader))) {
            continue;
        }
        buffer.resize(static_cast<size_t>(rec));

        RDTHeader hdr{};
        if (!parse_header(buffer, hdr)) {
            continue;
        }

        vector<char> data(buffer.begin() + sizeof(RDTHeader),
                               buffer.begin() + sizeof(RDTHeader) + hdr.length);
        uint16_t calc = rdt_checksum(hdr, data);
        if (calc != hdr.checksum) {
            // corrupción -> reenviar último ACK válido
            send_ack(udp, sender, state.lastAck);
            continue;
        }

        if (hdr.type == RDT_TYPE_ACK) {
            // ACK recibido por accidente aquí -> ignorar; lo manejará rdt_send
            continue;
        }

        // DATA
        if (hdr.seq != state.expectedSeq) {
            // duplicado -> reenvío último ACK
            send_ack(udp, sender, state.lastAck);
            continue;
        }

        payload = move(data);
        state.lastAck = hdr.seq;
        send_ack(udp, sender, hdr.seq);
        state.expectedSeq ^= 1;
        return true;
    }
}

// -------------- FUNCIONES object --------------

vector<char> serializeMochila(const Mochila& m) {
    vector<char> b;
    writeString(b, m.propietario);

    writeInt(b, m.cuaderno.paginas);
    writeBool(b, m.cuaderno.cuadriculado);
    b.insert(b.end(), m.cuaderno.color, m.cuaderno.color + 16);

    writeString(b, m.libro.titulo);
    writeInt(b, m.libro.anio);

    writeFloat(b, m.regla.longitud);
    writeBool(b, m.regla.metalica);

    b.push_back(m.lapiz.tipo);
    writeInt(b, m.lapiz.cantidad);

    writeString(b, m.refri.nombre);
    writeFloat(b, m.refri.calorias);
    writeBool(b, m.refri.saludable);
    return b;
}

Mochila deserializeMochila(const vector<char>& buf) {
    Mochila m;
    size_t off = 0;
    m.propietario = readString(buf, off);
    m.cuaderno.paginas = readInt(buf, off);
    m.cuaderno.cuadriculado = readBool(buf, off);
    memcpy(m.cuaderno.color, buf.data() + off, 16);
    off += 16;
    m.libro.titulo = readString(buf, off);
    m.libro.anio = readInt(buf, off);
    m.regla.longitud = readFloat(buf, off);
    m.regla.metalica = readBool(buf, off);
    m.lapiz.tipo = buf[off++];
    m.lapiz.cantidad = readInt(buf, off);
    m.refri.nombre = readString(buf, off);
    m.refri.calorias = readFloat(buf, off);
    m.refri.saludable = readBool(buf, off);
    return m;
}

void printMochila(const Mochila& m) {
    cout << "\nMOCHILA de " << m.propietario << ":\n";
    cout << " Cuaderno: " << m.cuaderno.paginas << " pág, "
         << (m.cuaderno.cuadriculado ? "Cuadriculado" : "Liso")
         << ", color=" << m.cuaderno.color << "\n";
    cout << " Libro: " << m.libro.titulo << " (" << m.libro.anio << ")\n";
    cout << " Regla: " << m.regla.longitud << " cm, "
         << (m.regla.metalica ? "Metálica" : "Plástica") << "\n";
    cout << " Lápiz: " << m.lapiz.tipo << ", cant=" << m.lapiz.cantidad << "\n";
    cout << " Refrigerio: " << m.refri.nombre << ", "
         << m.refri.calorias << " kcal, "
         << (m.refri.saludable ? "Saludable" : "No saludable") << "\n";
}

void sendMochila(UDP_Socket *udp, const string &propietario, const sockaddr_in &serverAddr) {

    string send_to;
    cout<<"\nSend Mochila to: ";
    getline(cin, send_to);

    Mochila m;
    m.propietario = "alex";
    m.cuaderno = {200, true, "Rojo"};
    m.libro = {"C++ Avanzado", 2022};
    m.regla = {30.5f, true};
    m.lapiz = {'B', 3};
    m.refri = {"Manzana", 52.5f, true};

    vector<char> obj = serializeMochila(m);
    string header = "o" + formatLength(send_to.size(), 2)
                      + send_to
                      + formatLength(obj.size(), 4);

    vector<char> packet(header.begin(), header.end());
    packet.insert(packet.end(), obj.begin(), obj.end());

    cout << "[CLIENT] send object of " << m.propietario
         << " (" << packet.size() << " bytes)\n";
    udp_enviar(udp, packet, &serverAddr);

    printMochila(m);

}

void receiveMochila(string& buffer) {
    size_t offset = 0;

    int len = stoi(buffer.substr(offset, 2)); offset += 2;
    string from = buffer.substr(offset, len); offset += len;

    int obj_size = stoi(buffer.substr(offset, 4)); offset += 4;

    vector<char> obj_data(buffer.begin() + offset, buffer.begin() + offset + obj_size);

    try {
        Mochila m = deserializeMochila(obj_data);
        cout << "\n[Client recieved from server (Mochila of " << from << ")]\n";
        printMochila(m);
    } catch (const exception &e) {
        cerr << "[ERROR en deserializeMochila] " << e.what() << endl;
    }
}

void sendFile(UDP_Socket *udp, const string propietario, const sockaddr_in &serverAddr) {
    string send_to, filename;
    cout<<"\nSend file to: ";
    getline(cin, send_to);
    cout<<"File path: ";
    getline(cin, filename);

    ifstream archivo(filename, ios::binary | ios::in);

    if (!archivo.is_open()) {
        cerr << "No se pudo abrir el archivo." << endl;
        return;
    }

    archivo.seekg(0, ios::end);
    streamsize fsize = archivo.tellg();
    archivo.seekg(0, ios::beg);


    string size_nick = send_to.size() > propietario.size() ? send_to : propietario;

    string header = "f" +
            formatLength(size_nick.size(), 2) + size_nick +
            formatLength(filename.size(), 3) + filename +
            formatLength(fsize, 10) +
            formatLength(0, 8);

    int i = 0;
    streamsize total_enviado = 0;
    
    while (true) {
        vector<char> chunk(MAXLINE-header.size());
        archivo.read(chunk.data(), MAXLINE-header.size());
        streamsize readed = archivo.gcount();
        if (readed <= 0) break;

        string header = "f" +
            formatLength(send_to.size(), 2) + send_to +
            formatLength(filename.size(), 3) + filename +
            formatLength(fsize, 10) +
            formatLength(i, 8);

        vector<char> datagram(header.begin(), header.end());
        datagram.insert(datagram.end(), chunk.begin(), chunk.begin() + readed);

        udp_enviar(udp, datagram, &serverAddr, false);

        total_enviado += readed;
        i++;

        cout << "Fragment " << i
                  << " send (" << readed << " bytes, "
                  << total_enviado << "/" << fsize << ")\n";

        usleep(3000); // pausa leve para controlar el envio de datagrams, la pc no es buena con varios datos a  la vez.
    }
    archivo.close();
}

void receiveFile(string buffer) {
    int len_to = obtener_longitud(buffer, 2);
    string to_nick = read_text(buffer, len_to);

    int len_fname = obtener_longitud(buffer, 3);
    string fname = read_text(buffer, len_fname);

    int total_size = obtener_longitud(buffer, 10);
    int pos = obtener_longitud(buffer, 8);

    string data = buffer;
    string key =  "from_" + to_nick + "_" + fname;
    
    if (udp_active_files.find(key) == udp_active_files.end()) {
        udp_active_files[key] = FilePart{
            fname, {}, static_cast<size_t>(total_size), 0
        };

        cout << "\n[UDP] Begin reception of file '" << fname
                  << "' (" << total_size << " bytes)" << endl;
    }

    auto &f = udp_active_files[key];

    if (pos >= f.chunks.size()) {
        f.chunks.resize(pos + 1);
    }

    if (f.chunks[pos].empty()) {
        f.chunks[pos] = move(data);
        f.received_bytes += f.chunks[pos].size();

        cout << " Fragment " << pos
                  << " received (" << f.chunks[pos].size() << " bytes)"
                  << "  [" << f.received_bytes << "/" << f.total_size << "]\n";
    }

    if (f.received_bytes >= f.total_size) {
        ofstream out(key, ios::binary);

        for (size_t i = 0; i < f.chunks.size(); ++i) {
            if (!f.chunks[i].empty())
                out.write(f.chunks[i].data(), f.chunks[i].size());
        }

        out.close();

        cout << "\n[UDP] File received completely: "
                  << f.fname << " (" << f.received_bytes << " bytes)\n";

        udp_active_files.erase(key);
    }
}