#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <string>
#include <map>
#include <vector>
#include <mutex>
#include <thread>
#include <cstring>
#include <unordered_map>
#include <chrono>
#include "extras.hpp"
#include "TicTacToe.hpp"

using namespace std;

// ============= CONFIGURACIÓN =============
constexpr int PORT = 45000;
// MAXLINE ya está definido en extras.hpp (777)

// ============= ESTRUCTURAS GLOBALES =============
map<string, sockaddr_in> current_users;        // nick -> addr
map<string, string> current_users_ids;         // "ip:port" -> nick
map<string, string> pending_invitations;       // inviter -> inviter (para tracking)

unordered_map<std::string, RDTState> rdtStates;
mutex rdtMutex; // opcional pero recomendable por los threads

TicTacToe ttt;

// ============= UTILIDADES =============
string addr_to_key(const sockaddr_in &addr) {
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr.sin_addr, ip, INET_ADDRSTRLEN);
    int port = ntohs(addr.sin_port);
    return string(ip) + ":" + to_string(port);
}

string longitudFormato(size_t len, int width) {
    string s(width, '0');
    for (int i = width - 1; i >= 0 && len > 0; --i) {
        s[i] = char('0' + (len % 10));
        len /= 10;
    }
    return s;
}

bool reliableSendTo(UDP_Socket* server,
                const vector<char>& payload,
                const sockaddr_in& dest,
                bool verbose)
{
    const string key = addr_to_key(dest);

    bool sent = false;
    {
        lock_guard<mutex> guard(rdtMutex);
        auto& state = rdtStates[key];
        sent = rdt_send(server, &dest, payload, state, 500, 5, verbose);
        if (!sent) {
            rdtStates.erase(key);
        }
    }

    if (!sent) {
        auto itId = current_users_ids.find(key);
        if (itId != current_users_ids.end()) {
            string nick = itId->second;
            current_users.erase(nick);
            current_users_ids.erase(itId);
            cout << "[Servidor] conexión perdida con '" << nick
                 << "' (timeout RDT)\n";
            ttt.handleDisconnect(server, current_users, nick);
        } else {
            cout << "[Servidor] conexión perdida con " << key
                 << " (timeout RDT)\n";
        }
    }

    return sent;
}

void sendErrorReliable(UDP_Socket* server, const sockaddr_in& dest, const string& msg) {
    string msg_error = string("E") + formatLength(msg.size(), 3) + msg;
    vector<char> payload(msg_error.begin(), msg_error.end());
    reliableSendTo(server, payload, dest);
}

// ============= MANEJO DE CLIENTES (THREAD) =============
void handleClient(UDP_Socket *server, vector<char> data, sockaddr_in cliaddr) {
    string key = addr_to_key(cliaddr);
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &cliaddr.sin_addr, ip, INET_ADDRSTRLEN);

    cout << "\n[Mensaje de " << key << "] ";

    string buffer(data.begin(), data.end());
    char cmd = read_text(buffer, 1)[0];
    
    if(cmd != 'f')
        cout << cmd << "..." << endl;

    // ============= REGISTRO DE USUARIO =============
    if (cmd == 'n') {
        int len = obtener_longitud(buffer, 2);
        string nick = read_text(buffer, len);

        cout << "Solicitud de registro: '" << nick << "'\n";

        auto it = current_users.find(nick);
        if (it != current_users.end()) {
            sendErrorReliable(server, cliaddr, "The nickname already exists.");
            cout << "   Nickname rechazado (ya existe)\n";
        } else {
            current_users[nick] = cliaddr;
            current_users_ids[key] = nick;
            vector<char> msg = {'O'};
            reliableSendTo(server,msg,cliaddr);
            cout << "  ✓ Usuario '" << nick << "' registrado\n";
        }
    }
    
    // ============= MENSAJE BROADCAST =============
    else if (cmd == 'm') {
        string from = current_users_ids[key];
        
        if (from.empty()) {
            cout << "   Usuario no registrado intenta broadcast\n";
            return;
        }

        int len_msg = obtener_longitud(buffer, 3);
        string msg = read_text(buffer, len_msg);

        cout << "Broadcast de '" << from << "': " << msg << "\n";

        // Enviar a TODOS excepto al remitente
        int sent_count = 0;
        for(auto const &[nick, addr] : current_users) {
            if (nick != from) {
                string msg_protocol = string("M") 
                    + formatLength(from.size(), 2) + from
                    + formatLength(msg.size(), 3) + msg;
                
                vector<char> msg_to_send(msg_protocol.begin(), msg_protocol.end());
                if (reliableSendTo(server, msg_to_send, addr)) {
                    sent_count++;
                }
            }
        }
        cout << "  ✓ Broadcast enviado a " << sent_count << " usuarios\n";

        string ackLabel = "broadcast(" + to_string(sent_count) + ")";
        string ackPayload = string("K") + 'S'
            + formatLength(ackLabel.size(), 2) + ackLabel
            + formatLength(msg.size(), 3) + msg;
        vector<char> ackVec(ackPayload.begin(), ackPayload.end());
        reliableSendTo(server, ackVec, cliaddr);
    }
    
    // ============= MENSAJE PRIVADO =============
    else if (cmd == 't') {
        string from = current_users_ids[key];
        
        if (from.empty()) {
            cout << "   Usuario no registrado intenta mensaje privado\n";
            return;
        }

        int len = obtener_longitud(buffer, 2);
        string to_send = read_text(buffer, len);

        int len_msg = obtener_longitud(buffer, 3);
        string msg = read_text(buffer, len_msg);

        cout << "Mensaje privado '" << from << "' -> '" << to_send << "': " << msg << "\n";

        auto it = current_users.find(to_send);
        if (it == current_users.end()) {
            sendErrorReliable(server, cliaddr, "The user nickname doesn't exist.");
            cout << "   Destinatario no existe\n";
        } else {
            string msg_protocol = string("T") 
                + formatLength(from.size(), 2) + from
                + formatLength(msg.size(), 3) + msg;

            vector<char> msg_to_send(msg_protocol.begin(), msg_protocol.end());
            if (reliableSendTo(server, msg_to_send, current_users[to_send])) {
                cout << "  ✓ Mensaje enviado a '" << to_send << "'\n";

                string ackPayload = string("K") + 'S'
                    + formatLength(to_send.size(), 2) + to_send
                    + formatLength(msg.size(), 3) + msg;
                vector<char> ackVec(ackPayload.begin(), ackPayload.end());
                reliableSendTo(server, ackVec, cliaddr);
            }
        }
    }
    
    // ============= LISTA DE USUARIOS =============
    else if (cmd == 'l') {
    cout << "Solicitud de lista de usuarios\n";
        
        string msg = "";
        for(auto const &u : current_users) {
            msg += formatLength(u.first.size(), 2) + u.first;
        }
        
        string msg_list = string("L") 
            + formatLength(current_users.size(), 2) + msg;
        
        vector<char> msg_to_send(msg_list.begin(), msg_list.end());
        reliableSendTo(server, msg_to_send, cliaddr);
        
        cout << "  ✓ Lista enviada (" << current_users.size() << " usuarios)\n";
    }
    
    // ============= TRANSFERENCIA DE ARCHIVO =============
    else if (cmd == 'f') {
        string from = current_users_ids[key];
        
        if (from.empty()) {
            cout << "   Usuario no registrado intenta enviar archivo\n";
            return;
        }

        int len_to = obtener_longitud(buffer, 2);
        string to_send = read_text(buffer, len_to);

        int len_fname = obtener_longitud(buffer, 3);
        string fname = read_text(buffer, len_fname);

        int fsize = obtener_longitud(buffer, 10);
        int position = obtener_longitud(buffer, 8);

        cout << "Archivo '" << fname << "' (" << fsize << " bytes) chunk #" 
             << position << " de '" << from << "' -> '" << to_send << "'\n";

        auto it = current_users.find(to_send);
        if (it == current_users.end()) {
            sendErrorReliable(server, cliaddr, "The user nickname doesn't exist.");
            cout << "   Destinatario no existe\n";
        } else {
            // Reenviar el chunk completo al destinatario
            vector<char> file_data(buffer.begin(), buffer.end());
            
            string header = string("F") 
                + formatLength(from.size(), 2) + from
                + formatLength(fname.size(), 3) + fname
                + formatLength(fsize, 10)
                + formatLength(position, 8);

            vector<char> msg_send(header.begin(), header.end());
            msg_send.insert(msg_send.end(), file_data.begin(), file_data.end());
            
            reliableSendTo(server, msg_send, it->second, false);
            cout << "  ✓ Chunk reenviado a '" << to_send << "'\n";
        }
    }
    
    // ============= ENVÍO DE OBJETO (MOCHILA) =============
    else if(cmd == 'o') {
        string from = current_users_ids[key];
        
        if (from.empty()) {
            cout << "   Usuario no registrado intenta enviar objeto\n";
            return;
        }

        int len_to = obtener_longitud(buffer, 2);
        string to_send = read_text(buffer, len_to);
        
        int len_obj = obtener_longitud(buffer, 4);
        vector<char> obj_data(buffer.begin(), buffer.begin() + len_obj);

        cout << "Objeto de '" << from << "' -> '" << to_send << "' (" << len_obj << " bytes)\n";
    
        auto it = current_users.find(to_send);
        if (it == current_users.end()) {
            sendErrorReliable(server, cliaddr, "The user nickname doesn't exist.");
            cout << "   Destinatario no existe\n";
        } else {
            string header = "O"
                + formatLength(from.size(), 2) + from
                + formatLength(len_obj, 4);

            vector<char> packet;
            packet.insert(packet.end(), header.begin(), header.end());
            packet.insert(packet.end(), obj_data.begin(), obj_data.end());

            reliableSendTo(server, packet, it->second);
            cout << "  ✓ Objeto enviado a '" << to_send << "'\n";
        }
    }
    
    // ============= TIC TAC TOE - JOIN =============
    else if(cmd == 'p') {
        string from = current_users_ids[key];
        
        if (from.empty()) {
            cout << "   Usuario no registrado intenta jugar TicTacToe\n";
            return;
        }

        cout << "TicTacToe: '" << from << "' quiere jugar\n";
        ttt.handleJoin(server, from, current_users);
    }
    
    // ============= TIC TAC TOE - MOVE =============
    else if(cmd == 'W') {
        string from = current_users_ids[key];
        
        if (from.empty()) {
            cout << "   Usuario no registrado intenta mover en TicTacToe\n";
            return;
        }

        char symbol = read_text(buffer, 1)[0];
        int pos = obtener_longitud(buffer, 1);
        
        cout << "TicTacToe: '" << from << "' mueve " << symbol << " en posición " << pos << "\n";
        ttt.handleMove(server, symbol, pos, from, current_users);
    }
    
    // ============= INVITACIÓN A TICTACTOE =============
    else if (cmd == 'I') {
        string from = current_users_ids[key];
        
        if (from.empty()) {
            cout << "   Usuario no registrado intenta invitar\n";
            return;
        }

        int len = obtener_longitud(buffer, 2);
        string inviter = read_text(buffer, len);
        
        // Verificar si ya hay un juego activo
        if (ttt.isGameActive()) {
            cout << " Invitación bloqueada: ya hay un juego activo\n";
            string error_msg = "o" + formatLength(50, 3) + "Ya hay un juego en curso. Usa opción 6 para ver.";
            vector<char> msg(error_msg.begin(), error_msg.end());
            reliableSendTo(server, msg, current_users[inviter]);
            return;
        }
        
        cout << "Invitación TicTacToe: '" << inviter << "' invita a todos\n";
        
        // Guardar invitación pendiente
        pending_invitations[inviter] = inviter;
        
        // Broadcast la invitación a todos los usuarios excepto el que invita
        string invitation = "I" + formatLength(inviter.size(), 2) + inviter;
        vector<char> msg(invitation.begin(), invitation.end());
        
        int sent_count = 0;
        for (auto &u : current_users) {
            if (u.first != inviter) {
                reliableSendTo(server, msg, u.second);
                sent_count++;
            }
        }
        
        cout << "  Invitación enviada a " << sent_count << " usuarios\n";
    }
    
    // ============= ACEPTAR INVITACIÓN =============
    else if (cmd == 'Y') {
        string from = current_users_ids[key];
        
        if (from.empty()) {
            cout << "   Usuario no registrado intenta aceptar invitación\n";
            return;
        }

        int len = obtener_longitud(buffer, 2);
        string accepter = read_text(buffer, len);
        
        cout << "Aceptación TicTacToe: '" << accepter << "' acepta jugar\n";
        
        // Buscar la invitación pendiente más reciente
        if (!pending_invitations.empty()) {
            auto it = pending_invitations.begin();
            string inviter = it->second;
            
            cout << "  ✓ Emparejando '" << inviter << "' con '" << accepter << "'\n";
            
            // Notificar al invitador que la partida inicia
            if (current_users.count(inviter)) {
                string start_msg = "S" + formatLength(accepter.size(), 2) + accepter;
                vector<char> msg_inviter(start_msg.begin(), start_msg.end());
                reliableSendTo(server, msg_inviter, current_users[inviter]);
            }
            
            // Notificar al aceptador que la partida inicia
            string start_msg = "S" + formatLength(inviter.size(), 2) + inviter;
            vector<char> msg_accepter(start_msg.begin(), start_msg.end());
            reliableSendTo(server, msg_accepter, current_users[accepter]);
            
            // Iniciar la partida de TicTacToe
            ttt.handleJoin(server, inviter, current_users);
            ttt.handleJoin(server, accepter, current_users);
            
            // Limpiar invitación pendiente
            pending_invitations.erase(it);
            
            cout << "  🎮 Partida iniciada entre '" << inviter << "' y '" << accepter << "'\n";
            
        } else {
            // No hay invitación pendiente
            cout << "   '" << accepter << "' aceptó tarde - ";
            
            // Verificar si hay un juego activo
            if (ttt.isGameActive()) {
                // Agregar como espectador
                ttt.addViewer(server, accepter, current_users);
                
                // Obtener nombres de los jugadores activos
                auto players = ttt.getActivePlayers();
                string player1 = players.size() > 0 ? players[0] : "?";
                string player2 = players.size() > 1 ? players[1] : "?";
                
                // Enviar mensaje informativo
                string info_msg = "Ya hay una partida entre " + player1 + " y " + player2 + ". Eres espectador.";
                string msg = "o" + formatLength(info_msg.size(), 3) + info_msg;
                vector<char> msg_send(msg.begin(), msg.end());
                reliableSendTo(server, msg_send, current_users[accepter]);
                
                cout << "Convertido en espectador de partida entre '" << player1 << "' y '" << player2 << "'\n";
                
            } else {
                // No hay invitación ni juego activo
                string error_msg = "La invitación ya expiró y no hay partida activa.";
                string msg = "o" + formatLength(error_msg.size(), 3) + error_msg;
                vector<char> msg_send(msg.begin(), msg.end());
                reliableSendTo(server, msg_send, current_users[accepter]);
                
                cout << "No hay partida activa\n";
            }
        }
    }
    
    // ============= RECHAZAR INVITACIÓN =============
    else if (cmd == 'N') {
        string from = current_users_ids[key];
        
        if (from.empty()) {
            cout << "   Usuario no registrado intenta rechazar invitación\n";
            return;
        }

        int len = obtener_longitud(buffer, 2);
        string rejecter = read_text(buffer, len);
        
        cout << "Rechazo TicTacToe: '" << rejecter << "' rechaza jugar\n";
        
        // Notificar al invitador del rechazo
        if (!pending_invitations.empty()) {
            auto it = pending_invitations.begin();
            string inviter = it->second;
            
            if (current_users.count(inviter)) {
                string reject_msg = "R" + formatLength(rejecter.size(), 2) + rejecter;
                vector<char> msg(reject_msg.begin(), reject_msg.end());
                reliableSendTo(server, msg, current_users[inviter]);
                cout << "   Notificación de rechazo enviada a '" << inviter << "'\n";
            }
        }
    }
    
    // ============= UNIRSE COMO ESPECTADOR =============
    else if (cmd == 'G') {
        string from = current_users_ids[key];
        
        if (from.empty()) {
            cout << "   Usuario no registrado intenta ver como espectador\n";
            return;
        }

        int len = obtener_longitud(buffer, 2);
        string viewer = read_text(buffer, len);
        
        cout << "Espectador TicTacToe: '" << viewer << "' quiere ver la partida\n";
        
        // Verificar si hay un juego activo
        if (ttt.isGameActive()) {
            ttt.addViewer(server, viewer, current_users);
        } else {
            cout << "   No hay juego activo para observar\n";
            string error_msg = "o" + formatLength(48, 3) + "No hay partida activa. ¡Envía una invitación!";
            vector<char> msg(error_msg.begin(), error_msg.end());
            reliableSendTo(server, msg, current_users[viewer]);
        }
    }
    
    // ============= DESCONEXIÓN =============
    else if (cmd == 'x' || cmd == 'd') {
        if (current_users_ids.count(key)) {
            string nick = current_users_ids[key];
            cout << "\n[Usuario desconectado: '" << nick << "']\n";
            
            current_users.erase(nick);
            current_users_ids.erase(key);
            {
                lock_guard<mutex> guard(rdtMutex);
                rdtStates.erase(key);
            }
            
            ttt.handleDisconnect(server, current_users, nick);
        }
    }
    
    // ============= COMANDO DESCONOCIDO =============
    else {
        cout << "   Comando desconocido: '" << cmd << "'\n";
    }
}

// ============= MAIN =============
int main() {
    // Crear socket usando extras.hpp
    UDP_Socket server = udp_crear_socket(PORT, NULL, 1);
    
    cout << "╔════════════════════════════════════════╗\n";
    cout << "║  Servidor UDP Mejorado - v2.0          ║\n";
    cout << "║  Puerto: " << PORT << "                ║\n";
    cout << "║  Protocolo: Simple (compatible)        ║\n";
    cout << "║  Threading: Activado                   ║\n";
    cout << "╚════════════════════════════════════════╝\n\n";
    cout << "Esperando conexiones...\n\n";

    while (true) {
        sockaddr_in peekAddr{};
        socklen_t peekLen = sizeof(peekAddr);
        char peekBuf[sizeof(RDTHeader)]{};

        int peekRes = recvfrom(server.sockfd, peekBuf, sizeof(peekBuf), MSG_PEEK,
                               reinterpret_cast<sockaddr*>(&peekAddr), &peekLen);
        if (peekRes <= 0) {
            continue;
        }

        if (peekRes < static_cast<int>(sizeof(RDTHeader))) {
            // Paquete inválido: descartamos leyendo y seguimos.
            char discard[MAXLINE];
            peekLen = sizeof(peekAddr);
            recvfrom(server.sockfd, discard, sizeof(discard), 0,
                     reinterpret_cast<sockaddr*>(&peekAddr), &peekLen);
            continue;
        }

        uint8_t peekType = static_cast<uint8_t>(peekBuf[0]);
        if (peekType == RDT_TYPE_ACK) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        vector<char> payload;
        sockaddr_in cliaddr{};
        {
            lock_guard<mutex> guard(rdtMutex);
            auto& state = rdtStates[addr_to_key(peekAddr)];
            if (!rdt_recv(&server, payload, &cliaddr, state)) {
                continue;
            }
        }

        thread(handleClient, &server, std::move(payload), cliaddr).detach();
    }

    close(server.sockfd);
    return EXIT_SUCCESS;
}
