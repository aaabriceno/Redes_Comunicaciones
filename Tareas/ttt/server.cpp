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
#include <map>
#include <vector>
#include <algorithm> // [TTT] for std::count

using namespace std;

#define PORT 45000
map<string, int> current_users;
map<int, string> current_users_ids;

static int ttt_sock_O = -1, ttt_sock_X = -1;
static char ttt_tablero[9] = {'1','2','3','4','5','6','7','8','9'};
static char ttt_turno = 'O';
static std::map<int,char> ttt_rol; // clientSock -> 'O'/'X'/'S'

static void ttt_resetear(){
    for(int i=0;i<9;i++) {
        ttt_tablero[i] = '1'+i;
    }
    ttt_turno = 'O';
    ttt_sock_O = ttt_sock_X = -1;
    ttt_rol.clear();
}

static bool ttt_verificarGanar(char p){
    int ganar_ttt[8][3]={{0,1,2},{3,4,5},{6,7,8},{0,3,6},{1,4,7},{2,5,8},{0,4,8},{2,4,6}};
    for(auto &x: ganar_ttt) {
        if(ttt_tablero[x[0]]==p && ttt_tablero[x[1]]==p && ttt_tablero[x[2]]==p){
            return true;
        }
    }
    return false;
}

static std::string ttt_board_str(){
    std::string s; s.reserve(9);
    for(int i=0;i<9;i++) s += (ttt_tablero[i]=='O'||ttt_tablero[i]=='X')? ttt_tablero[i] : '_';
    return s;
}

static void ttt_send(int sock, const std::string& s){
    if(sock>=0) send(sock, s.c_str(), s.size(), 0);
}

static bool ttt_tablero_lleno(){
    for(int i = 0; i < 9; i++){
        if(ttt_tablero[i] != 'O' && ttt_tablero[i] != 'X'){
            return false;
        }
    }
    return true;
}

static void ttt_broadcast(const std::string& s, const std::map<std::string,int>& current_users){
    for(const auto& kv : current_users) send(kv.second, s.c_str(), s.size(), 0);
}

string formatLength(size_t len, int cifras) {
    stringstream ss;
    ss << setw(cifras) << setfill('0') << len;
    return ss.str();
}

int get_len(int clientSock, int n_prot){
    char readed[16];
    read(clientSock, readed, n_prot);
    readed[n_prot] = '\0';
    return atoi(readed);
}

string read_text(int clientSock, int len){
    char *readed = new char[len+1];
    read(clientSock, readed, len);
    readed[len] = '\0';
    string s(readed);
    delete[] readed;
    return s;
}

void newClientThread(int clientSock) {
    char buffer[2];
    int n;

    while (true) {
        n = read(clientSock, buffer, 1);
        if (n <= 0) {
            if(current_users_ids.count(clientSock)){
                cout << "\n[Cliente desconectado: " << current_users_ids[clientSock] << "]" << endl;
                current_users.erase(current_users_ids[clientSock]);
                current_users_ids.erase(clientSock);
            }
            break;
        }
        buffer[n] = '\0';
        string command_char(buffer); // <-- Para usar en los cout

        // [TTT] ---- Protocol handlers ----
        if (buffer[0] == 'n') {
            int len = get_len(clientSock, 2);
            string nick = read_text(clientSock, len);
            cout << " Recibido del cliente ==> " << command_char << formatLength(len,2) << nick << endl; // <-- LÍNEA AÑADIDA

            if (current_users.count(nick)){
                string msg = "Ese nickname ya existe.";
                string msg_error = string("E") + formatLength(msg.size(),3) + msg;
                cout << " Enviando al cliente ==> " << msg_error << endl; // <-- LÍNEA AÑADIDA
                write(clientSock, msg_error.c_str(), msg_error.size());
            } else {
                current_users[nick] = clientSock;
                current_users_ids[clientSock] = nick;
                cout << "[Nuevo usuario conectado: " << nick << "]" << endl;
            }
        } 
        
        else if (buffer[0] == 'P') { // Petición para jugar
            string from = current_users_ids[clientSock];
            cout << " Recibido del cliente ==> " << buffer[0] << endl;
            
            // Si ya hay un juego en curso
            if (ttt_sock_O >= 0 && ttt_sock_X >= 0) {
                string msg = "Ya hay un juego en curso. Te unes como espectador.";
                string msg_error = string("E") + formatLength(msg.size(),3) + msg;
                write(clientSock, msg_error.c_str(), msg_error.size());
                
                char role = 'S';
                ttt_rol[clientSock] = role;
                string roleMsg = std::string("ROLE ") + role + "\n";
                send(clientSock, roleMsg.c_str(), roleMsg.size(), 0);
                std::string b = std::string("v ") + ttt_board_str() + "\n";
                send(clientSock, b.c_str(), b.size(), 0);
                continue;
            }
            
            // Si no hay ningún jugador, este será O y se hace broadcast
            if (ttt_sock_O < 0 && ttt_sock_X < 0) {
                ttt_sock_O = clientSock;
                ttt_rol[clientSock] = 'O';
                
                cout << "[" << from << " quiere jugar TTT - Enviando invitación a todos]" << endl;
                
                // Broadcast preguntando quién quiere jugar
                string invite_msg = string("I") + formatLength(from.size(), 2) + from;
                for(auto const &u : current_users) {
                    if (u.second != clientSock) { // No enviar a quien inició
                        cout << " Enviando invitación a (" << u.first << ") ==> " << invite_msg << endl;
                        write(u.second, invite_msg.c_str(), invite_msg.size());
                    }
                }
                
                // Confirmar al iniciador
                string roleMsg = std::string("ROLE ") + 'O' + "\n";
                send(clientSock, roleMsg.c_str(), roleMsg.size(), 0);
                string waiting = "Esperando oponente...\n";
                send(clientSock, waiting.c_str(), waiting.size(), 0);
            }
            
            continue;
        } 
        
        else if (buffer[0] == 'A') { // Aceptar invitación TTT
            string from = current_users_ids[clientSock];
            cout << " Recibido del cliente ==> " << buffer[0] << endl;
            
            // Solo si hay un jugador O esperando y no hay X
            if (ttt_sock_O >= 0 && ttt_sock_X < 0) {
                ttt_sock_X = clientSock;
                ttt_rol[clientSock] = 'X';
                
                cout << "[" << from << " acepta jugar contra " << current_users_ids[ttt_sock_O] << "]" << endl;
                
                // Enviar roles
                string roleO = std::string("ROLE ") + 'O' + "\n";
                string roleX = std::string("ROLE ") + 'X' + "\n";
                send(ttt_sock_O, roleO.c_str(), roleO.size(), 0);
                send(ttt_sock_X, roleX.c_str(), roleX.size(), 0);
                
                // Enviar tablero inicial
                std::string b = std::string("v ") + ttt_board_str() + "\n";
                ttt_broadcast(b, current_users);
                
                // Iniciar turno
                std::string t = "T O\n";
                ttt_broadcast(t, current_users);
                
                // Notificar que el juego comenzó
                string game_start = "¡El juego ha comenzado!\n";
                ttt_broadcast(game_start, current_users);
            } 
            else {
                string msg = "No hay invitación disponible o ya hay otro oponente.";
                string msg_error = string("E") + formatLength(msg.size(),3) + msg;
                write(clientSock, msg_error.c_str(), msg_error.size());
            }
            continue;
        }

        else if (buffer[0] == 'M') { // Movimiento: M <rol> <pos>
            // Read the rest of the line (expecting: ' ' <role> ' ' <digit> [\n])
            char rest[8]={0};
            int r = read(clientSock, rest, sizeof(rest)-1);
            char jugador='?', sp=' ', sp2=' '; int pos = -1;
            if (r >= 3) {
                // naive parse: " O 5"
                jugador = rest[1];
                if (r >= 3 && rest[2]==' ') {
                    if (r >= 4 && rest[3]>='1' && rest[3]<='9') pos = rest[3]-'0';
                }
            }
            if (jugador != ttt_turno) { send(clientSock, "V F\n", 4, 0); continue; }
            if (pos < 1 || pos > 9) { send(clientSock, "V F\n", 4, 0); continue; }
            int idx = pos-1;
            if (ttt_tablero[idx]=='O' || ttt_tablero[idx]=='X') { send(clientSock, "V F\n", 4, 0); continue; }
            // Apply move
            ttt_tablero[idx] = jugador;
            send(clientSock, "V T\n", 4, 0);
            // Broadcast board
            std::string b = std::string("v ") + ttt_board_str() + "\n";
            ttt_broadcast(b, current_users);
            // Check win
            if (ttt_verificarGanar(jugador)) {
                std::string ganar_ttt = std::string("WIN ") + jugador + "\n";
                ttt_broadcast(ganar_ttt, current_users);
                // reset for next game (optional)
                // ttt_resetear();
            } 
            
            else if (ttt_tablero_lleno()) {
                string empate = "DRAW\n";
                ttt_broadcast(empate, current_users);
                // ttt_resetear(); // opcional
            } 
            
            else {
                ttt_turno = (ttt_turno=='O') ? 'X' : 'O';
                std::string t = std::string("T ") + ttt_turno + "\n";
                ttt_broadcast(t, current_users);
            }
            continue;
        }

        else if (buffer[0] == 'm') {
            string from = current_users_ids[clientSock];
            int len_msg = get_len(clientSock, 3); 
            string msg = read_text(clientSock, len_msg);
            cout << " Recibido del cliente ==> " << command_char << formatLength(len_msg,3) << msg << endl; // <-- LÍNEA AÑADIDA
            
            for(auto const &u : current_users){
                if (u.first != from){
                    string msg_list = string("M") + formatLength(from.size(),2) + from + formatLength(msg.size(),3) + msg;
                    cout << " Enviando al cliente (" << u.first << ") ==> " << msg_list << endl; // <-- LÍNEA AÑADIDA
                    write(u.second, msg_list.c_str(), msg_list.size());
                }
            }
        } 
        
        else if (buffer[0] == 't') {
            int len = get_len(clientSock, 2);
            string to_send = read_text(clientSock, len);
            int len_msg = get_len(clientSock, 3);
            string msg = read_text(clientSock, len_msg);
            string from = current_users_ids[clientSock];

            cout << " Recibido del cliente ==> " << command_char << formatLength(len,2) << to_send << formatLength(len_msg,3) << msg << endl; // <-- LÍNEA AÑADIDA

            if (!current_users.count(to_send)){
                string error_msg = "El nickname del destinatario no existe.";
                string msg_error = string("E") + formatLength(error_msg.size(),3) + error_msg;
                cout << " Enviando al cliente ==> " << msg_error << endl; // <-- LÍNEA AÑADIDA
                write(clientSock, msg_error.c_str(), msg_error.size());
            } else {
                string msg_to_send = string("T") + formatLength(from.size(), 2) + from + formatLength(msg.size(),3) + msg;
                cout << " Enviando al cliente (" << to_send << ") ==> " << msg_to_send << endl; // <-- LÍNEA AÑADIDA
                write(current_users[to_send], msg_to_send.c_str(), msg_to_send.size());
            }
        } 
        
        else if (buffer[0] == 'l') {
            cout << " Recibido del cliente ==> " << command_char << endl; // <-- LÍNEA AÑADIDA
            string msg = "";
            for(auto const &u : current_users){
                msg += formatLength(u.first.size(), 2) + u.first;
            }
            string msg_list = string("L") + formatLength(current_users.size(),2) + msg;
            cout << " Enviando al cliente ==> " << msg_list << endl; // <-- LÍNEA AÑADIDA
            write(clientSock, msg_list.c_str(), msg_list.size());

        } 
        
        else if (buffer[0] == 'f') { 
            int len_to = get_len(clientSock, 2);
            string to_send = read_text(clientSock, len_to);
            int len_fname = get_len(clientSock, 3);
            string fname = read_text(clientSock, len_fname);
            int fsize = get_len(clientSock, 10);
            string file_hash = read_text(clientSock, 64);
            string from = current_users_ids[clientSock];

            cout << " Recibido del cliente (cabecera) ==> " << command_char << formatLength(len_to,2) << to_send << formatLength(len_fname,3) << fname << formatLength(fsize,10) << file_hash << endl; // <-- LÍNEA AÑADIDA
            cout << "Retransmitiendo archivo de " << from << " a " << to_send << endl;

            if (!current_users.count(to_send)) {
                string error_msg = "El destinatario no existe.";
                string msg_error = string("E") + formatLength(error_msg.size(),3) + error_msg;
                cout << " Enviando al cliente ==> " << msg_error << endl; // <-- LÍNEA AÑADIDA
                write(clientSock, msg_error.c_str(), msg_error.size());
                
                char temp_buf[1024];
                int recvd = 0;
                while (recvd < fsize) {
                    int r = read(clientSock, temp_buf, min(1024, fsize - recvd));
                    if (r <= 0) break;
                    recvd += r;
                }
            } 
            
            else {
                string header = string("F") + formatLength(from.size(),2) + from
                            + formatLength(fname.size(),3) + fname
                            + formatLength(fsize,10) + file_hash;
                cout << " Enviando al cliente (cabecera) ==> " << header << endl; // <-- LÍNEA AÑADIDA
                write(current_users[to_send], header.c_str(), header.size());

                char file_buffer[1024];
                int recvd = 0;
                while (recvd < fsize) {
                    int r = read(clientSock, file_buffer, min(1024, fsize - recvd));
                    if (r <= 0) break;
                    write(current_users[to_send], file_buffer, r);
                    recvd += r;
                }
                cout << " -> Retransmisión completada." << endl;
            }
        } 
        
        else if (buffer[0] == 'x') {
            cout << " Recibido del cliente ==> " << command_char << endl; // <-- LÍNEA AÑADIDA
            cout << "\n[Usuario se ha desconectado: " << current_users_ids[clientSock] << "]" << endl;
            current_users.erase(current_users_ids[clientSock]);
            current_users_ids.erase(clientSock);
            shutdown(clientSock, SHUT_RDWR);
            close(clientSock);
            break;
        } 
        
        else if (buffer[0] == 'o') { 
            /*
            string from = current_users_ids[clientSock];
            int len_to = get_len(clientSock, 2);
            string to_send = read_text(clientSock, len_to);

            cout << "\nRecibido comando de objeto binario de [" << from << "] para [" << to_send << "]" << endl;
            
            // --- 1. Reconstruir el objeto leyendo los bytes ---
            sala sala_reconstruida;
            sala_reconstruida.c = new sala::cocina();

            // Leer miembros de 'sala'
            read(clientSock, &sala_reconstruida.n, sizeof(sala_reconstruida.n));
            int len_str;
            read(clientSock, &len_str, sizeof(len_str));
            read(clientSock, sala_reconstruida.str, len_str);
            sala_reconstruida.str[len_str] = '\0';

            // Leer miembros de 'cocina'
            read(clientSock, &sala_reconstruida.c->cocinaNum, sizeof(sala_reconstruida.c->cocinaNum));
            int len_cocina_vec;
            read(clientSock, &len_cocina_vec, sizeof(len_cocina_vec));
            read(clientSock, sala_reconstruida.c->cocinaVector, len_cocina_vec);
            sala_reconstruida.c->cocinaVector[len_cocina_vec] = '\0';
            read(clientSock, &sala_reconstruida.c->cocinaFLotante, sizeof(sala_reconstruida.c->cocinaFLotante));

            // --- 2. ¡Éxito! Imprimir en el servidor para verificar ---
            cout << "--- Objeto reconstruido en SERVIDOR ---" << endl;
            cout << "  sala.str: " << sala_reconstruida.str << endl;
            cout << "  cocina.cocinaVector: " << sala_reconstruida.c->cocinaVector << endl;
            cout << "------------------------------------" << endl;

            // --- 3. REENVIAR EL OBJETO AL CLIENTE DESTINO ---
            if (current_users.count(to_send)) { // Verifica si el usuario destino está conectado
                int dest_sock = current_users[to_send];
                
                // Protocolo: 'O' + [len_from(2)] + [from_user] + [datos binarios...]
                char command = 'O';
                string from_msg = formatLength(from.size(), 2) + from;

                cout << "Enviando objeto a [" << to_send << "] ==> " << command << from_msg << "[...datos binarios...]" << endl;

                // Escribir el comando y el remitente
                write(dest_sock, &command, 1);
                write(dest_sock, from_msg.c_str(), from_msg.size());

                // Escribir los datos binarios del objeto
                write(dest_sock, &sala_reconstruida.n, sizeof(sala_reconstruida.n));
                write(dest_sock, &len_str, sizeof(len_str));
                write(dest_sock, sala_reconstruida.str, len_str);
                write(dest_sock, &sala_reconstruida.c->cocinaNum, sizeof(sala_reconstruida.c->cocinaNum));
                write(dest_sock, &len_cocina_vec, sizeof(len_cocina_vec));
                write(dest_sock, sala_reconstruida.c->cocinaVector, len_cocina_vec);
                write(dest_sock, &sala_reconstruida.c->cocinaFLotante, sizeof(sala_reconstruida.c->cocinaFLotante));

            } else {
                cout << "Usuario [" << to_send << "] no encontrado. No se pudo reenviar el objeto." << endl;
            }

            // Limpieza de memoria
            delete sala_reconstruida.c;
            */
        }

        else if (buffer[0] == 'J') { // Jugada TTT
            if (ttt_rol.count(clientSock) && ttt_rol[clientSock] != 'S') {
                char player = ttt_rol[clientSock];
                if (player == ttt_turno) { // Verificar que sea su turno
                    char pos_char;
                    read(clientSock, &pos_char, 1);
                    int pos = pos_char - '1'; // Convertir a índice 0-8
                    
                    if (pos >= 0 && pos <= 8 && ttt_tablero[pos] != 'O' && ttt_tablero[pos] != 'X') {
                        ttt_tablero[pos] = player;
                        
                        // Broadcast del tablero actualizado
                        string board_msg = "B " + ttt_board_str() + "\n";
                        ttt_broadcast(board_msg, current_users);
                        
                        // Verificar ganador
                        if (ttt_verificarGanar(player)) {
                            string winner_msg = "W" + string(1, player) + "\n";
                            ttt_broadcast(winner_msg, current_users);
                            ttt_resetear();
                        } 
                        else if (ttt_tablero_lleno()) {
                            string draw_msg = "WDRAW\n"; // Empate
                            ttt_broadcast(draw_msg, current_users);
                            ttt_resetear();
                        }
                        else {
                            // Cambiar turno
                            ttt_turno = (ttt_turno == 'O') ? 'X' : 'O';
                            string turn_msg = "G " + string(1, ttt_turno) + "\n";
                            ttt_broadcast(turn_msg, current_users);
                        }
                    }
                }
            }
            continue;
        }
    }
}

int main(void) {
    struct sockaddr_in serverAddr;
    int serverSock, clientSock;
    vector<thread> threads;

    serverSock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSock == -1) { perror("No se pudo crear el socket"); exit(EXIT_FAILURE); }

    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(serverSock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == -1) {
        perror("Fallo en bind"); close(serverSock); exit(EXIT_FAILURE);
    }
    if (listen(serverSock, 10) == -1) {
        perror("Fallo en listen"); close(serverSock); exit(EXIT_FAILURE);
    }
    cout << "Servidor escuchando en el puerto " << PORT << "...\n";

    while (true) {
        clientSock = accept(serverSock, NULL, NULL);
        threads.emplace_back(newClientThread, clientSock);
    }
    for (auto& t : threads) {
        t.join();
    }
    close(serverSock);
    return 0;
}
