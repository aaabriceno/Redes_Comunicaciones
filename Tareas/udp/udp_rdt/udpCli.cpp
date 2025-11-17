#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <csignal>
#include <iostream>
#include <fstream>
#include <limits>
#include <string>
#include <thread>
#include <cstdlib>
#include <mutex>
#include "extras.hpp"

using namespace std;

// ============= CONFIGURACIÓN =============
constexpr const char* SERVER_IP = "127.0.0.1";
constexpr int PORT = 45000;

// ============= VARIABLES GLOBALES =============
string globalNickname;
UDP_Socket client;
sockaddr_in serverAddr;
bool myTurn = false;
char mySymbol;
bool inGame = false;  // Si está en una partida activa
bool waitingResponse = false;  // Si está esperando una respuesta Y/N de invitación


RDTState clientRdtState;
mutex clientSendMutex;
mutex clientRecvMutex;  // sincroniza lecturas del socket
bool receivingFile = false;

bool reliableSend(const vector <char>& payload, bool verbose = false){
    lock_guard<mutex> sendGuard(clientSendMutex);
    lock_guard<mutex> recvGuard(clientRecvMutex);
    return rdt_send(&client, &serverAddr, payload, clientRdtState, 500, 5, verbose);
}

bool reliableReceive(vector<char>& payload, uint32_t timeoutMs = 0){
    lock_guard<mutex> recvGuard(clientRecvMutex);
    sockaddr_in sender = serverAddr;
    return rdt_recv(&client, payload, &sender, clientRdtState, timeoutMs);
}

// ============= MANEJO DE SEÑALES =============
void handle_exit(int signum) {
    if (!globalNickname.empty()) {
        cout << "\n\n[Desconectando del servidor...]\n";
        vector<char> msg = {'x'};
        reliableSend(msg);
    }
    close(client.sockfd);
    cout << "Adiós!\n";
    exit(signum);
}

// ============= UTILIDADES =============
string calcularSHA256(const string& filename) {
    const string cmd = "sha256sum \"" + filename + "\" 2>/dev/null > temp_hash.txt";
    if (system(cmd.c_str()) != 0) return "";
    
    ifstream in("temp_hash.txt");
    string hash;
    if (in) in >> hash;
    in.close();
    remove("temp_hash.txt");
    return hash;
}

// ============= REGISTRO DE USUARIO =============
bool registrarNombre() {
    vector<char> buffer;
    
    while (true) {
        cout << "Ingresa tu Nickname (o 'q' para salir): ";
        getline(cin, globalNickname);
        
        if (globalNickname.empty()) {
            cerr << "Nickname vacío. Inténtalo de nuevo.\n";
            continue;
        }
        
        if (globalNickname == "q" || globalNickname == "Q") {
            cout << "Saliendo.\n";
            return false;
        }

        // Enviar solicitud de registro
        string nickMsg = string("n") + formatLength(globalNickname.size(), 2) + globalNickname;
        vector<char> send(nickMsg.begin(), nickMsg.end());
        reliableSend(send);

        // Esperar respuesta
        if (!reliableReceive(buffer, 2000)) {
            cout << "\n[Error] Sin respuesta del servidor.\n";
            continue;
        }

        if (buffer[0] == 'E') {
            cout << "\nError: El nickname ya existe. Intenta con otro.\n\n";
        } else if (buffer[0] == 'O') {
            cout << "\n✓ Nickname registrado correctamente como: " << globalNickname << "\n";
            return true;
        }
    }
}

// ============= MENÚ PRINCIPAL =============
void showMenu() {
    cout << "\n╔════════════════════════════════╗\n";
    cout << "║         MENÚ PRINCIPAL         ║\n";
    cout << "╠════════════════════════════════╣\n";
    cout << "║ 1) Ver Usuarios Conectados     ║\n";
    cout << "║ 2) Mensaje Privado             ║\n";
    cout << "║ 3) Mensaje Global (Broadcast)  ║\n";
    cout << "║ 4) Enviar Archivo              ║\n";
    cout << "║ 5) Enviar Objeto (Mochila)     ║\n";
    cout << "║ 6) Jugar TicTacToe             ║\n";
    cout << "║ 7) Salir                       ║\n";
    cout << "╚════════════════════════════════╝\n";
    cout << "Opción: " << flush;
}

// ============= THREAD DE RECEPCIÓN =============
void readThread() {
    vector<char> data;
    
    while (true) {
        if (!reliableReceive(data, 50)) {
            usleep(1000);
            continue;
        }

        string buffer(data.begin(), data.end());
        string buff = read_text(buffer, 1);

        if (buff[0] == 'L') {
            // Lista de usuarios
            int len = obtener_longitud(buffer, 2);
            cout << "\n\n==== Usuarios Conectados ====\n";
            for(int i = 0; i < len; i++) {
                int len_nick = obtener_longitud(buffer, 2);
                string nick = read_text(buffer, len_nick);
                cout << "  • " << nick;
                if(globalNickname == nick) cout << " (tú)";
                cout << endl;
            }
            cout << "============================\n";

        } else if (buff[0] == 'T' || buff[0] == 'M') {
            // Mensaje privado o broadcast
            int len = obtener_longitud(buffer, 2);
            string from = read_text(buffer, len);
            int len_msg = obtener_longitud(buffer, 3);
            string msg = read_text(buffer, len_msg);

            cout << "\n\n";
            if (buff[0] == 'M') cout << " [MENSAJE GLOBAL] ";
            else cout << "MSG ";
            cout << "[" << from << "]: " << msg << "\n";
            cout << " [ACK local] Confirmación automática registrada.\n";

        } else if (buff[0] == 'F') {
            // Recepción de archivo
            FileReceiveEvent evt = receiveFile(buffer);
            if (evt == FileReceiveEvent::Fragment) {
                receivingFile = true;
            } else if (evt == FileReceiveEvent::Completed) {
                receivingFile = false;
            }

        } else if(buff[0] == 'O') {
            // Recepción de objeto
            receiveMochila(buffer);

        } else if(buff[0] == 'E') {
            // Error del servidor
            int len = obtener_longitud(buffer, 3);
            string msg = read_text(buffer, len);
            cout << "\n  Error del servidor: " << msg << "\n";

        } else if (buff[0] == 'v') {
            // Tablero de TicTacToe
            string board = read_text(buffer, 9);
            cout << "\n[TicTacToe - Estado del tablero]\n";
            cout << " " << board[0] << " | " << board[1] << " | " << board[2] << "\n";
            cout << "---+---+---\n";
            cout << " " << board[3] << " | " << board[4] << " | " << board[5] << "\n";
            cout << "---+---+---\n";
            cout << " " << board[6] << " | " << board[7] << " | " << board[8] << "\n";

        } else if (buff[0] == 'o') {
            // Game Over o mensaje informativo
            int len = obtener_longitud(buffer, 3);
            string msg = read_text(buffer, len);
            
            // Detectar si es Game Over o mensaje informativo
            if (msg.find("Game Over") != string::npos || msg.find("win") != string::npos || 
                msg.find("lose") != string::npos || msg.find("draw") != string::npos) {
                cout << "\n [Game Over] " << msg << "\n";
                myTurn = false;
                inGame = false;  // Partida terminada, volver a menú normal
            } else {
                // Mensaje informativo (ej: "Eres espectador")
                cout << "\n [Info] " << msg << "\n";
            }

        } else if (buff[0] == 'V') {
            // Es tu turno
            mySymbol = read_text(buffer, 1)[0];
            cout << "\n Es tu turno (" << mySymbol << "). Usa opción 6 para jugar.\n";
            myTurn = true;
            inGame = true;
            
        } else if (buff[0] == 'I') {
            // Invitación a jugar TicTacToe
            int len = obtener_longitud(buffer, 2);
            string inviter = read_text(buffer, len);
            
            waitingResponse = true;  // Esperando respuesta Y/N
            
            cout << "\n\n╔════════════════════════════════════════╗\n";
            cout << "║     INVITACIÓN A TICTACTOE          ║\n";
            cout << "╠════════════════════════════════════════╣\n";
            cout << "║  " << inviter << " te invita a jugar!";
            // Padding para alinear
            for(int i = inviter.size(); i < 22; i++) cout << " ";
            cout << "║\n";
            cout << "╠════════════════════════════════════════╣\n";
            cout << "║  Escribe 'Y' para ACEPTAR              ║\n";
            cout << "║  Escribe 'N' para RECHAZAR             ║\n";
            cout << "╚════════════════════════════════════════╝\n";
            cout << "Tu respuesta (Y/N): " << flush;
            
        } else if (buff[0] == 'S') {
            // Partida iniciada
            int len = obtener_longitud(buffer, 2);
            string opponent = read_text(buffer, len);
            
            cout << "\n\n ¡Partida iniciada contra " << opponent << "!\n";
            cout << "Esperando tu turno...\n";
            inGame = true;
            waitingResponse = false;  // Ya no esperamos respuesta
            
        } else if (buff[0] == 'R') {
            // Invitación rechazada
            int len = obtener_longitud(buffer, 2);
            string rejecter = read_text(buffer, len);
            
            cout << "\n " << rejecter << " rechazó tu invitación.\n";
        } else if (buff[0] == 'K') {
            string ackSourceStr = read_text(buffer, 1);
            char ackSource = ackSourceStr.empty() ? '?' : ackSourceStr[0];
            int len_name = obtener_longitud(buffer, 2);
            string name = read_text(buffer, len_name);
            int len_msg = obtener_longitud(buffer, 3);
            string msg = read_text(buffer, len_msg);

            if (ackSource == 'S' || ackSource == 'R') {
                cout << "\n [ACK servidor] " << name
                     << " recibió: " << msg << "\n";
            } else {
                cout << "\n [ACK] " << name << ": " << msg << "\n";
            }
        }

        // Mostrar menú solo si NO estamos en juego y NO esperando respuesta Y/N
        if (!inGame && !waitingResponse && !receivingFile) {
            showMenu();
        } else if (inGame) {
            // Si estamos en juego, solo mostrar prompt simple
            cout << "\nOpción: " << flush;
        }
        // Si waitingResponse == true, no mostramos nada (ya se mostró "Tu respuesta (Y/N):")

    }
}

// ============= ENVÍO DE ARCHIVO CON SHA-256 =============
void enviarArchivoConVerificacion() {
    string sendTo, filepath;
    cout << "\nEnviar archivo a (nickname): ";
    getline(cin, sendTo);
    cout << "Ruta del archivo: ";
    getline(cin, filepath);

    ifstream file(filepath, ios::binary);
    if(!file) {
        cerr << " Error: No se pudo abrir el archivo.\n";
        return;
    }

    // Calcular hash SHA-256 ANTES de enviar
    string hash = calcularSHA256(filepath);
    if(hash.size() != 64) {
        cerr << " Error: Hash SHA-256 inválido.\n";
        file.close();
        return;
    }

    // Obtener tamaño del archivo
    file.seekg(0, ios::end);
    size_t fsize = static_cast<size_t>(file.tellg());
    file.seekg(0, ios::beg);

    // Extraer nombre del archivo
    string filename = filepath;
    size_t slash = filepath.find_last_of("/\\");
    if (slash != string::npos) {
        filename = filepath.substr(slash + 1);
    }

    // Mostrar información del archivo
    cout << "\n Enviando archivo: " << filename << " (" << fsize << " bytes)\n";
    cout << " SHA-256: " << hash << "\n";
    cout << " Destinatario: " << sendTo << "\n\n";

    // Enviar archivo chunk por chunk
    int i = 0;
    size_t total_enviado = 0;
    
    while (true) {
        vector<char> chunk(MAXLINE - 100); // Dejar espacio para header
        file.read(chunk.data(), MAXLINE - 100);
        streamsize readed = file.gcount();
        if (readed <= 0) break;

        string header = string("f")
            + formatLength(sendTo.size(), 2) + sendTo
            + formatLength(filename.size(), 3) + filename
            + formatLength(fsize, 10)
            + formatLength(i, 8);

        vector<char> datagram(header.begin(), header.end());
        datagram.insert(datagram.end(), chunk.begin(), chunk.begin() + readed);

        reliableSend(datagram,false);

        total_enviado += readed;
        i++;

        cout << "Fragmento " << i << " enviado (" << readed << " bytes, "
             << total_enviado << "/" << fsize << ")\n";

        usleep(3000); // Pausa breve entre chunks
    }
    
    file.close();
    cout << "\n✓ Archivo enviado completamente (" << total_enviado << " bytes en " << i << " fragmentos)\n";
    cout << "ℹ  Hash SHA-256 del archivo: " << hash << "\n";
}

// ============= MAIN =============
int main() {
    // Configurar manejo de señales
    signal(SIGINT, handle_exit);

    // Crear socket UDP
    client = udp_crear_socket(0, NULL, 0);

    // Configurar dirección del servidor
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    serverAddr.sin_addr.s_addr = inet_addr(SERVER_IP);

    cout << "╔════════════════════════════════════════╗\n";
    cout << "║  Cliente UDP - Chat Mejorado           ║\n";
    cout << "║  Conectado a: " << SERVER_IP << ":" << PORT << "     ║\n";
    cout << "╚════════════════════════════════════════╝\n\n";

    // Registrar nickname
    if (!registrarNombre()) {
        close(client.sockfd);
        return EXIT_SUCCESS;
    }

    // Iniciar thread de recepción
    thread reader(readThread);
    reader.detach();

    // Bucle principal del menú
    string buf;
    int option;
    bool running = true;
    string input;

    while (running) {
        showMenu();
        
        // Leer entrada como string para detectar Y/N
        getline(cin, input);
        
        // Si es Y o N, manejar respuesta a invitación
        if (input == "Y" || input == "y") {
            string msg = "Y" + formatLength(globalNickname.size(), 2) + globalNickname;
            vector<char> msg_send(msg.begin(), msg.end());
            reliableSend(msg_send);
            cout << "\n Aceptaste la invitación. Esperando confirmación...\n";
            waitingResponse = false;  // Ya respondimos
            continue;
            
        } else if (input == "N" || input == "n") {
            string msg = "N" + formatLength(globalNickname.size(), 2) + globalNickname;
            vector<char> msg_send(msg.begin(), msg.end());
            reliableSend(msg_send);
            cout << "\n Rechazaste la invitación.\n";
            waitingResponse = false;  // Ya respondimos
            continue;
        }
        
        // Si no es Y/N, intentar convertir a número
        try {
            option = stoi(input);
        } catch (...) {
            cout << " Opción inválida. Intenta de nuevo.\n";
            continue;
        }

        if (option == 7) {
            // Salir
            vector<char> msg = {'x'};
            reliableSend(msg);
            cout << "\n Desconectando...\n";
            running = false;

        } else if (option == 1) {
            // Lista de usuarios
            vector<char> msg = {'l'};
           reliableSend(msg);

        } else if (option == 2) {
            // Mensaje privado
            string send_to;
            cout << "\nEnviar mensaje a: ";
            getline(cin, send_to);
            cout << "Mensaje: ";
            getline(cin, buf);
            
            string msg = string("t") + formatLength(send_to.size(), 2) + send_to 
                       + formatLength(buf.size(), 3) + buf;
            vector<char> msg_send(msg.begin(), msg.end());
            reliableSend(msg_send);
            cout << "✓ Mensaje enviado a " << send_to << "\n";

        } else if (option == 3) {
            // Broadcast
            cout << "\nMensaje para todos: ";
            getline(cin, buf);
            
            string msg = string("m") + formatLength(buf.size(), 3) + buf;
            vector<char> msg_send(msg.begin(), msg.end());
            reliableSend(msg_send);
            cout << "✓ Mensaje broadcast enviado\n";

        } else if (option == 4) {
            // Enviar archivo
            enviarArchivoConVerificacion();

        } else if (option == 5) {
            // Enviar objeto (Mochila)
            sendMochila(&client, globalNickname, serverAddr);

        } else if (option == 6) {
            // TicTacToe
            if (inGame && myTurn) {
                // Si estoy en partida y es mi turno, hago mi movimiento
                int pos;
                cout << "Ingresa posición (1-9): ";
                cin >> pos;
                cin.ignore(numeric_limits<streamsize>::max(), '\n');

                string move = "W";
                move.push_back(mySymbol);
                move += formatLength(pos, 1);

                vector<char> msg_send(move.begin(), move.end());
                reliableSend(msg_send);
                myTurn = false;
                
            } else if (!inGame) {
                // No estoy en partida, mostrar menú de invitación/espectador
                cout << "\n╔════════════════════════════════════════╗\n";
                cout << "║           TICTACTOE                  ║\n";
                cout << "╠════════════════════════════════════════╣\n";
                cout << "║  1. Enviar invitación a todos          ║\n";
                cout << "║  2. Ver partida como espectador        ║\n";
                cout << "║  3. Cancelar                           ║\n";
                cout << "╚════════════════════════════════════════╝\n";
                cout << "Elige una opción: ";
                
                int subOption;
                cin >> subOption;
                cin.ignore(numeric_limits<streamsize>::max(), '\n');
                
                if (subOption == 1) {
                    // Enviar invitación broadcast
                    string msg = "I" + formatLength(globalNickname.size(), 2) + globalNickname;
                    vector<char> msg_send(msg.begin(), msg.end());
                    reliableSend(msg_send);
                    cout << "\n Invitación enviada a todos los jugadores.\n";
                    cout << "⏳ Esperando respuestas...\n";
                    
                } else if (subOption == 2) {
                    // Unirse como espectador
                    string msg = "G" + formatLength(globalNickname.size(), 2) + globalNickname;
                    vector<char> msg_send(msg.begin(), msg.end());
                    reliableSend(msg_send);
                    cout << "\n Solicitando unirse como espectador...\n";
                }
            } else {
                // Estoy en partida pero no es mi turno
                cout << "\n Esperando el turno del oponente...\n";
            }

        } else {
            cout << " Opción inválida. Intenta de nuevo.\n";
        }
    }

    close(client.sockfd);
    return EXIT_SUCCESS;
}
