#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <string>
#include <map>
#include <thread>
#include <cstring>
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
        int len = get_len(buffer, 2);
        string nick = read_text(buffer, len);

        cout << "Solicitud de registro: '" << nick << "'\n";

        auto it = current_users.find(nick);
        if (it != current_users.end()) {
            sendError(server, &cliaddr, "The nickname already exists.");
            cout << "  ❌ Nickname rechazado (ya existe)\n";
        } else {
            current_users[nick] = cliaddr;
            current_users_ids[key] = nick;
            vector<char> msg = {'O'};
            udp_send(server, msg, &cliaddr);
            cout << "  ✓ Usuario '" << nick << "' registrado\n";
        }
    }
    
    // ============= MENSAJE BROADCAST =============
    else if (cmd == 'm') {
        string from = current_users_ids[key];
        
        if (from.empty()) {
            cout << "  ⚠️ Usuario no registrado intenta broadcast\n";
            return;
        }

        int len_msg = get_len(buffer, 3);
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
                udp_send(server, msg_to_send, &addr);
                sent_count++;
            }
        }
        cout << "  ✓ Broadcast enviado a " << sent_count << " usuarios\n";
    }
    
    // ============= MENSAJE PRIVADO =============
    else if (cmd == 't') {
        string from = current_users_ids[key];
        
        if (from.empty()) {
            cout << "  ⚠️ Usuario no registrado intenta mensaje privado\n";
            return;
        }

        int len = get_len(buffer, 2);
        string to_send = read_text(buffer, len);

        int len_msg = get_len(buffer, 3);
        string msg = read_text(buffer, len_msg);

        cout << "Mensaje privado '" << from << "' -> '" << to_send << "': " << msg << "\n";

        auto it = current_users.find(to_send);
        if (it == current_users.end()) {
            sendError(server, &cliaddr, "The user nickname doesn't exist.");
            cout << "  ❌ Destinatario no existe\n";
        } else {
            string msg_protocol = string("T") 
                + formatLength(from.size(), 2) + from
                + formatLength(msg.size(), 3) + msg;

            vector<char> msg_to_send(msg_protocol.begin(), msg_protocol.end());
            udp_send(server, msg_to_send, &current_users[to_send]);
            cout << "  ✓ Mensaje enviado a '" << to_send << "'\n";
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
        udp_send(server, msg_to_send, &cliaddr);
        
        cout << "  ✓ Lista enviada (" << current_users.size() << " usuarios)\n";
    }
    
    // ============= TRANSFERENCIA DE ARCHIVO =============
    else if (cmd == 'f') {
        string from = current_users_ids[key];
        
        if (from.empty()) {
            cout << "  ⚠️ Usuario no registrado intenta enviar archivo\n";
            return;
        }

        int len_to = get_len(buffer, 2);
        string to_send = read_text(buffer, len_to);

        int len_fname = get_len(buffer, 3);
        string fname = read_text(buffer, len_fname);

        int fsize = get_len(buffer, 10);
        int position = get_len(buffer, 8);

        cout << "Archivo '" << fname << "' (" << fsize << " bytes) chunk #" 
             << position << " de '" << from << "' -> '" << to_send << "'\n";

        auto it = current_users.find(to_send);
        if (it == current_users.end()) {
            sendError(server, &cliaddr, "The user nickname doesn't exist.");
            cout << "  ❌ Destinatario no existe\n";
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
            
            udp_send(server, msg_send, &it->second, false);
            cout << "  ✓ Chunk reenviado a '" << to_send << "'\n";
        }
    }
    
    // ============= ENVÍO DE OBJETO (MOCHILA) =============
    else if(cmd == 'o') {
        string from = current_users_ids[key];
        
        if (from.empty()) {
            cout << "  ⚠️ Usuario no registrado intenta enviar objeto\n";
            return;
        }

        int len_to = get_len(buffer, 2);
        string to_send = read_text(buffer, len_to);
        
        int len_obj = get_len(buffer, 4);
        vector<char> obj_data(buffer.begin(), buffer.begin() + len_obj);

        cout << "Objeto de '" << from << "' -> '" << to_send << "' (" << len_obj << " bytes)\n";
    
        auto it = current_users.find(to_send);
        if (it == current_users.end()) {
            sendError(server, &cliaddr, "The user nickname doesn't exist.");
            cout << "  ❌ Destinatario no existe\n";
        } else {
            string header = "O"
                + formatLength(from.size(), 2) + from
                + formatLength(len_obj, 4);

            vector<char> packet;
            packet.insert(packet.end(), header.begin(), header.end());
            packet.insert(packet.end(), obj_data.begin(), obj_data.end());

            udp_send(server, packet, &it->second);
            cout << "  ✓ Objeto enviado a '" << to_send << "'\n";
        }
    }
    
    // ============= TIC TAC TOE - JOIN =============
    else if(cmd == 'p') {
        string from = current_users_ids[key];
        
        if (from.empty()) {
            cout << "  ⚠️ Usuario no registrado intenta jugar TicTacToe\n";
            return;
        }

        cout << "TicTacToe: '" << from << "' quiere jugar\n";
        ttt.handleJoin(server, from, current_users);
    }
    
    // ============= TIC TAC TOE - MOVE =============
    else if(cmd == 'W') {
        string from = current_users_ids[key];
        
        if (from.empty()) {
            cout << "  ⚠️ Usuario no registrado intenta mover en TicTacToe\n";
            return;
        }

        char symbol = read_text(buffer, 1)[0];
        int pos = get_len(buffer, 1);
        
        cout << "TicTacToe: '" << from << "' mueve " << symbol << " en posición " << pos << "\n";
        ttt.handleMove(server, symbol, pos, from, current_users);
    }
    
    // ============= INVITACIÓN A TICTACTOE =============
    else if (cmd == 'I') {
        string from = current_users_ids[key];
        
        if (from.empty()) {
            cout << "  ⚠️ Usuario no registrado intenta invitar\n";
            return;
        }

        int len = get_len(buffer, 2);
        string inviter = read_text(buffer, len);
        
        // Verificar si ya hay un juego activo
        if (ttt.isGameActive()) {
            cout << "⚠️ Invitación bloqueada: ya hay un juego activo\n";
            string error_msg = "o" + formatLength(50, 3) + "Ya hay un juego en curso. Usa opción 6 para ver.";
            vector<char> msg(error_msg.begin(), error_msg.end());
            udp_send(server, msg, &current_users[inviter]);
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
                udp_send(server, msg, &u.second);
                sent_count++;
            }
        }
        
        cout << "  📢 Invitación enviada a " << sent_count << " usuarios\n";
    }
    
    // ============= ACEPTAR INVITACIÓN =============
    else if (cmd == 'Y') {
        string from = current_users_ids[key];
        
        if (from.empty()) {
            cout << "  ⚠️ Usuario no registrado intenta aceptar invitación\n";
            return;
        }

        int len = get_len(buffer, 2);
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
                udp_send(server, msg_inviter, &current_users[inviter]);
            }
            
            // Notificar al aceptador que la partida inicia
            string start_msg = "S" + formatLength(inviter.size(), 2) + inviter;
            vector<char> msg_accepter(start_msg.begin(), start_msg.end());
            udp_send(server, msg_accepter, &current_users[accepter]);
            
            // Iniciar la partida de TicTacToe
            ttt.handleJoin(server, inviter, current_users);
            ttt.handleJoin(server, accepter, current_users);
            
            // Limpiar invitación pendiente
            pending_invitations.erase(it);
            
            cout << "  🎮 Partida iniciada entre '" << inviter << "' y '" << accepter << "'\n";
            
        } else {
            // No hay invitación pendiente
            cout << "  ⚠️ '" << accepter << "' aceptó tarde - ";
            
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
                udp_send(server, msg_send, &current_users[accepter]);
                
                cout << "Convertido en espectador de partida entre '" << player1 << "' y '" << player2 << "'\n";
                
            } else {
                // No hay invitación ni juego activo
                string error_msg = "La invitación ya expiró y no hay partida activa.";
                string msg = "o" + formatLength(error_msg.size(), 3) + error_msg;
                vector<char> msg_send(msg.begin(), msg.end());
                udp_send(server, msg_send, &current_users[accepter]);
                
                cout << "No hay partida activa\n";
            }
        }
    }
    
    // ============= RECHAZAR INVITACIÓN =============
    else if (cmd == 'N') {
        string from = current_users_ids[key];
        
        if (from.empty()) {
            cout << "  ⚠️ Usuario no registrado intenta rechazar invitación\n";
            return;
        }

        int len = get_len(buffer, 2);
        string rejecter = read_text(buffer, len);
        
        cout << "Rechazo TicTacToe: '" << rejecter << "' rechaza jugar\n";
        
        // Notificar al invitador del rechazo
        if (!pending_invitations.empty()) {
            auto it = pending_invitations.begin();
            string inviter = it->second;
            
            if (current_users.count(inviter)) {
                string reject_msg = "R" + formatLength(rejecter.size(), 2) + rejecter;
                vector<char> msg(reject_msg.begin(), reject_msg.end());
                udp_send(server, msg, &current_users[inviter]);
                cout << "  ✉️ Notificación de rechazo enviada a '" << inviter << "'\n";
            }
        }
    }
    
    // ============= UNIRSE COMO ESPECTADOR =============
    else if (cmd == 'G') {
        string from = current_users_ids[key];
        
        if (from.empty()) {
            cout << "  ⚠️ Usuario no registrado intenta ver como espectador\n";
            return;
        }

        int len = get_len(buffer, 2);
        string viewer = read_text(buffer, len);
        
        cout << "Espectador TicTacToe: '" << viewer << "' quiere ver la partida\n";
        
        // Verificar si hay un juego activo
        if (ttt.isGameActive()) {
            ttt.addViewer(server, viewer, current_users);
        } else {
            cout << "  ⚠️ No hay juego activo para observar\n";
            string error_msg = "o" + formatLength(48, 3) + "No hay partida activa. ¡Envía una invitación!";
            vector<char> msg(error_msg.begin(), error_msg.end());
            udp_send(server, msg, &current_users[viewer]);
        }
    }
    
    // ============= DESCONEXIÓN =============
    else if (cmd == 'x' || cmd == 'd') {
        if (current_users_ids.count(key)) {
            string nick = current_users_ids[key];
            cout << "\n[Usuario desconectado: '" << nick << "']\n";
            
            current_users.erase(nick);
            current_users_ids.erase(key);
            
            ttt.handleDisconnect(server, current_users, nick);
        }
    }
    
    // ============= COMANDO DESCONOCIDO =============
    else {
        cout << "  ⚠️ Comando desconocido: '" << cmd << "'\n";
    }
}

// ============= MAIN =============
int main() {
    // Crear socket usando extras.hpp
    UDP_Socket server = udp_create_socket(PORT, NULL, 1);
    
    cout << "╔════════════════════════════════════════╗\n";
    cout << "║  Servidor UDP Mejorado - v2.0          ║\n";
    cout << "║  Puerto: " << PORT << "                          ║\n";
    cout << "║  Protocolo: Simple (compatible)        ║\n";
    cout << "║  Threading: Activado                   ║\n";
    cout << "╚════════════════════════════════════════╝\n\n";
    cout << "Esperando conexiones...\n\n";

    while (true) {
        sockaddr_in cliaddr;
        vector<char> buffer;

        int n = udp_receive(&server, buffer, &cliaddr);
        if (n <= 0) continue;

        // Crear un thread para manejar cada petición
        thread(handleClient, &server, buffer, cliaddr).detach();
    }

    close(server.sockfd);
    return EXIT_SUCCESS;
}
