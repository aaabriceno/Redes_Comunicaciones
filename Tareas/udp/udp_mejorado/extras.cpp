#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <cstdint>
#include <sstream>
#include <fstream>

#include <unistd.h>        // read, write, close, sendto, recvfrom
#include <sys/types.h>     // tipos de socket
#include <sys/socket.h>    // funciones de socket
#include <netinet/in.h>    // sockaddr_in
#include <arpa/inet.h>     // inet_addr, htons, etc.    
#include "extras.hpp"

using namespace std;
std::unordered_map<std::string, FilePart> udp_active_files;


// ============> UDP :
UDP_Socket udp_create_socket(int port, const char *ip, int isServer) {
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


void udp_send(UDP_Socket* udp, const vector<char>& data, const sockaddr_in* dest, bool verbose) {
    vector<char> msg = data;
    if (msg.size() < MAXLINE) {
        msg.insert(msg.end(), MAXLINE - msg.size(), '#');
    }
    
    if (verbose){
        for(int i=0;i<MAXLINE;i++) cout<<msg[i];
        cout<<endl;
        cout<<std::string(data.begin(), data.end())<<endl;
    }

    sendto(udp->sockfd, msg.data(), msg.size(), 0, (const sockaddr*)dest, sizeof(*dest));
}

int udp_receive(UDP_Socket* udp, vector<char>& buffer, sockaddr_in* sender) {
    buffer.assign(MAXLINE, 0);
    socklen_t len = sizeof(*sender);
    int n = recvfrom(udp->sockfd, buffer.data(), MAXLINE, 0, (sockaddr*)sender, &len);
    if (n <= 0) return -1;

    if (buffer[0]!='f' && buffer[0]!='F'){
        for(int i=0;i<MAXLINE;i++) cout<<buffer[i];
        cout<<endl;
    }

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

int get_len(string& buffer, int n_prot){
    string readed = buffer.substr(0,n_prot);
    buffer.erase(0,n_prot);
    return stoi(readed);
}

string read_text(string& buffer, int len){
    string s = buffer.substr(0,len);
    buffer.erase(0,len);
    return s;
}

void sendError(UDP_Socket* udp, sockaddr_in* sender, std::string msg_){
    string msg_error = string("E") + formatLength(msg_.size(),3) + msg_;
    //cout<<" /nEnviando  al cliente ==> "<<msg_error<<endl;
    vector<char> msg(msg_error.begin(), msg_error.end());
    udp_send(udp, msg, sender);
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

void sendMochila(UDP_Socket *udp, const string &propietario
    , const sockaddr_in &serverAddr) {

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
    udp_send(udp, packet, &serverAddr);

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
    } catch (const std::exception &e) {
        cerr << "[ERROR en deserializeMochila] " << e.what() << endl;
    }
}

void sendFile(UDP_Socket *udp, const string propietario, const sockaddr_in &serverAddr) {
    string send_to, filename;
    cout<<"\nSend file to: ";
    getline(cin, send_to);
    cout<<"File path: ";
    getline(cin, filename);

    std::ifstream archivo(filename, std::ios::binary | std::ios::in);

    if (!archivo.is_open()) {
        std::cerr << "No se pudo abrir el archivo." << std::endl;
        return;
    }

    archivo.seekg(0, std::ios::end);
    std::streamsize fsize = archivo.tellg();
    archivo.seekg(0, std::ios::beg);


    string size_nick = send_to.size() > propietario.size() ? send_to : propietario;

    std::string header = "f" +
            formatLength(size_nick.size(), 2) + size_nick +
            formatLength(filename.size(), 3) + filename +
            formatLength(fsize, 10) +
            formatLength(0, 8);

    int i = 0;
    std::streamsize total_enviado = 0;
    
    while (true) {
        std::vector<char> chunk(MAXLINE-header.size());
        archivo.read(chunk.data(), MAXLINE-header.size());
        std::streamsize readed = archivo.gcount();
        if (readed <= 0) break;

        std::string header = "f" +
            formatLength(send_to.size(), 2) + send_to +
            formatLength(filename.size(), 3) + filename +
            formatLength(fsize, 10) +
            formatLength(i, 8);

        std::vector<char> datagram(header.begin(), header.end());
        datagram.insert(datagram.end(), chunk.begin(), chunk.begin() + readed);

        udp_send(udp, datagram, &serverAddr, false);

        total_enviado += readed;
        i++;

        std::cout << "Fragment " << i
                  << " send (" << readed << " bytes, "
                  << total_enviado << "/" << fsize << ")\n";

        usleep(3000); // pausa leve para controlar el envio de datagrams, la pc no es buena con varios datos a  la vez.
    }
    archivo.close();
}

void receiveFile(std::string buffer) {
    int len_to = get_len(buffer, 2);
    std::string to_nick = read_text(buffer, len_to);

    int len_fname = get_len(buffer, 3);
    std::string fname = read_text(buffer, len_fname);

    int total_size = get_len(buffer, 10);
    int pos = get_len(buffer, 8);

    std::string data = buffer;
    std::string key =  "from_" + to_nick + "_" + fname;
    
    if (udp_active_files.find(key) == udp_active_files.end()) {
        udp_active_files[key] = FilePart{
            fname, {}, static_cast<size_t>(total_size), 0
        };

        std::cout << "\n[UDP] Begin reception of file '" << fname
                  << "' (" << total_size << " bytes)" << std::endl;
    }

    auto &f = udp_active_files[key];

    if (pos >= f.chunks.size()) {
        f.chunks.resize(pos + 1);
    }

    if (f.chunks[pos].empty()) {
        f.chunks[pos] = std::move(data);
        f.received_bytes += f.chunks[pos].size();

        std::cout << " Fragment " << pos
                  << " received (" << f.chunks[pos].size() << " bytes)"
                  << "  [" << f.received_bytes << "/" << f.total_size << "]\n";
    }

    if (f.received_bytes >= f.total_size) {
        std::ofstream out(key, std::ios::binary);

        for (size_t i = 0; i < f.chunks.size(); ++i) {
            if (!f.chunks[i].empty())
                out.write(f.chunks[i].data(), f.chunks[i].size());
        }

        out.close();

        std::cout << "\n[UDP] File received completely: "
                  << f.fname << " (" << f.received_bytes << " bytes)\n";

        udp_active_files.erase(key);
    }
}