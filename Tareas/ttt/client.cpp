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
#include <fstream>
#include <limits>
#include <vector> 
//#include "claseSala.cpp"

using namespace std;

#define PORT 45000
#define SERVER_IP "127.0.0.1"

string global_nickname;
static char myTTTRole = 'S';

void showMenu() {
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

string calculate_sha256_from_file(const string& filename) {
    string command = "sha256sum \"" + filename + "\" > temp_hash.txt";
    system(command.c_str());
    ifstream hash_file("temp_hash.txt");
    string hash_leido = "";
    if (hash_file.is_open()) {
        hash_file >> hash_leido;
        hash_file.close();
    }
    remove("temp_hash.txt");
    return hash_leido;
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

void registerNickname(int sockfd){
    int n;
    char buffer[2];
    struct timeval tv;
    tv.tv_sec = 1;   
    tv.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
    while (1) {
        cout << "Enter your Nickname: ";
        getline(cin, global_nickname);
        string nickMsg = string("n") + formatLength(global_nickname.size(), 2) + global_nickname;
        write(sockfd, nickMsg.c_str(), nickMsg.size());
        cout<<" Enviando al servidor ==> "<<nickMsg<<endl; 
        n = read(sockfd, buffer, 1);
        if (n <= 0) {
            cout << "\nNickname accepted!\n\n";
            break;
        }
        buffer[n] = '\0';
        if (buffer[0] == 'E') {
            int len = get_len(sockfd, 3);
            string msg = read_text(sockfd, len);
            cout << "\nError msg => " << msg << "\n\n";
        }
    }
    struct timeval tv_reset;
    tv_reset.tv_sec = 0;
    tv_reset.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv_reset, sizeof tv_reset);   
}

void printTTTBoard(string& s){
    cout << "\n╔═══╦═══╦═══╗\n";
    for(int i = 0; i < 9; i += 3){
        cout << "║ ";
        for(int j = 0; j < 3; j++){
            char c = s[i+j];
            if(c == '_') c = '1' + i + j; // Mostrar números para posiciones vacías
            cout << c << " ║ ";
        }
        cout << "\n";
        if(i < 6) cout << "╠═══╬═══╬═══╣\n";
    }
    cout << "╚═══╩═══╩═══╝\n";
}

void readThread(int socketConn) {
    char buffer[2];
    int n;
    while (true) {
        n = read(socketConn, buffer, 1);
        if (n <= 0) {
            cout << "\n[Server disconnected]\n";
            break;
        }
        buffer[n] = '\0';

        if (buffer[0] == 'L') {
            int len = get_len(socketConn, 2);
            cout<<endl<<"\n ---- Current Users ----\n";
            for(int i=0; i<len; i++){
                int len_nick = get_len(socketConn, 2);
                string nick = read_text(socketConn, len_nick);
                cout<<" - "<<nick;
                if(global_nickname==nick) cout<<" (tú)";
                cout<<endl;
            }
            showMenu();
        } 
        
        else if (buffer[0] == 'T' || buffer[0] == 'M') {
            int len = get_len(socketConn, 2);
            string from = read_text(socketConn, len);
            int len_msg = get_len(socketConn, 3);
            string msg = read_text(socketConn, len_msg);
            cout<<"\n\n";
            if (buffer[0] == 'M') cout<<"MENSAJE GLOBAL ";
            cout <<"["<< from<<": "<< msg <<"]\n"<< endl;
            showMenu();
        } 
        
        else if (buffer[0] == 'F') {
            int len_from = get_len(socketConn, 2);
            string from = read_text(socketConn, len_from);
            int len_fname = get_len(socketConn, 3);
            string fname = read_text(socketConn, len_fname);
            int fsize = get_len(socketConn, 10);
            string received_hash = read_text(socketConn, 64);

            cout << "\n[Archivo entrante de: " << from << "]" << endl;
            cout << " -> Nombre: " << fname << " (" << fsize << " bytes)" << endl;
            
            string base_name = fname;
            string extension = "";
            size_t dot_pos = fname.rfind('.');
            if (dot_pos != string::npos) {
                base_name = fname.substr(0, dot_pos);
                extension = fname.substr(dot_pos);
            }
            string save_as = base_name + "_" + global_nickname + extension;
            
            cout << " -> Guardando como: " << save_as << endl;
            
            FILE* fp = fopen(save_as.c_str(), "wb");
            char file_buffer[1024];
            int recvd = 0;
            while (recvd < fsize) {
                int r = read(socketConn, file_buffer, min(1024, fsize - recvd));
                if (r <= 0) break;
                fwrite(file_buffer, 1, r, fp);
                recvd += r;
            }
            fclose(fp);
            cout << "\n[Archivo recibido correctamente]" << endl;

            string calculated_hash = calculate_sha256_from_file(save_as);
            cout << " -> Hash recibido:    " << received_hash << endl;
            cout << " -> Hash calculado:   " << calculated_hash << endl;

            if (received_hash == calculated_hash) {
                cout << " -> [ÉXITO] El archivo está íntegro." << endl;
            } else {
                cout << " -> [ERROR] El archivo está corrupto. Los hashes no coinciden." << endl;
            }
            showMenu();
        } 
        
        else if(buffer[0]=='E') {
            int len = get_len(socketConn, 3);
            string msg = read_text(socketConn, len);
            cout <<"\n\nError del servidor => "<< msg << "\n\n";
            showMenu();
        } 
        
        else if (buffer[0]=='O'){ 
            /*
            int from_len = get_len(socketConn, 2);
            string from_user = read_text(socketConn, from_len);
            
            cout << "\n\n<=== Objeto Recibido de [" << from_user << "] ===>" << endl;

            // --- Reconstruir el objeto leyendo los bytes ---
            sala sala_recibida;
            sala_recibida.c = new sala::cocina();

            read(socketConn, &sala_recibida.n, sizeof(sala_recibida.n));
            int len_str;
            read(socketConn, &len_str, sizeof(len_str));
            read(socketConn, sala_recibida.str, len_str);
            sala_recibida.str[len_str] = '\0';

            read(socketConn, &sala_recibida.c->cocinaNum, sizeof(sala_recibida.c->cocinaNum));
            int len_cocina_vec;
            read(socketConn, &len_cocina_vec, sizeof(len_cocina_vec));
            read(socketConn, sala_recibida.c->cocinaVector, len_cocina_vec);
            sala_recibida.c->cocinaVector[len_cocina_vec] = '\0';
            read(socketConn, &sala_recibida.c->cocinaFLotante, sizeof(sala_recibida.c->cocinaFLotante));

            // --- ¡Éxito! Imprimir el contenido del objeto ---
            cout << "  Contenido de la Sala:" << endl;
            cout << "    - n: " << sala_recibida.n << endl;
            cout << "    - str: '" << sala_recibida.str << "'" << endl;
            cout << "  Contenido de la Cocina:" << endl;
            cout << "    - cocinaNum: " << sala_recibida.c->cocinaNum << endl;
            cout << "    - cocinaVector: '" << sala_recibida.c->cocinaVector << "'" << endl;
            cout << "    - cocinaFlotante: " << static_cast<float>(sala_recibida.c->cocinaFLotante) << endl;
            cout << "<====================================>" << endl;
            
            delete sala_recibida.c;
            */
           showMenu();
        }
        
        else if (buffer[0] == 'R'){
            char space, rol;
            read(socketConn, &space, 1);
            read(socketConn, &rol, 1);
            myTTTRole = rol;
            cout << "\n[TTT] Tu rol: " << myTTTRole << endl;
            if (myTTTRole == 'S') {
                cout << "Eres espectador de este juego." << endl;
            }
            showMenu();
        }

        else if (buffer[0] == 'B'){
            char space;
            string board_data;
            read(socketConn, &space, 1); // Leer espacio
            char temp;
            while (read(socketConn, &temp, 1) > 0) {
                if (temp == '\n') break;
                board_data += temp;
            }
            if (board_data.length() == 9) {
                printTTTBoard(board_data);
            }
        }

        else if (buffer[0] == 'W'){
            char winner;
            int r = read(socketConn, &winner, 1);
            if (r > 0){
                cout << "\n\n[TTT] 🏆 Ganó: " << winner << "\n";
            }
        }

        else if (buffer[0] == 'I') { // Invitación para jugar TTT
            int len_from = get_len(socketConn, 2);
            string from_user = read_text(socketConn, len_from);
            
            cout << "\n\n🎮 [INVITACIÓN TTT] 🎮" << endl;
            cout << "'" << from_user << "' quiere jugar Tic-Tac-Toe contigo." << endl;
            cout << "¿Deseas jugar? (s/n): " << flush;
            
            string response;
            getline(cin, response);
            
            if (response == "s" || response == "S" || response == "si" || response == "SI") {
                char accept = 'A';
                write(socketConn, &accept, 1);
                cout << " Enviando al servidor ==> " << accept << endl;
                cout << "Aceptando invitación..." << endl;
            } else {
                cout << "Invitación rechazada." << endl;
            }
            showMenu();
        }
        
        else{
            //showMenu();
        }
        //showMenu();
    }
}

void sendFile(int sockfd) {
    string send_to, filepath;
    cout<<"\nEnviar archivo a (nickname): ";
    getline(cin, send_to);
    cout<<"Ruta del archivo: ";
    getline(cin, filepath);

    ifstream file_check(filepath);
    if (!file_check.good()) {
        cout << "Error: No se pudo abrir el archivo en la ruta: " << filepath << endl;
        return;
    }
    file_check.close();

    string file_hash = calculate_sha256_from_file(filepath);
    if (file_hash.empty() || file_hash.length() != 64) {
        cout << "Error: No se pudo calcular el hash. Asegúrate de que 'sha256sum' esté instalado en tu sistema." << endl;
        return;
    }
    
    ifstream file(filepath, ifstream::binary);
    file.seekg(0, file.end);
    long fsize = file.tellg();
    file.close();
    
    string filename = filepath;
    size_t last_slash = filepath.find_last_of("/\\");
    if (last_slash != string::npos) {
        filename = filepath.substr(last_slash + 1);
    }

    string header = string("f") 
                  + formatLength(send_to.size(), 2) + send_to
                  + formatLength(filename.size(), 3) + filename
                  + formatLength(fsize, 10)
                  + file_hash;

    cout<<" Enviando al servidor (cabecera) ==> "<<header<<endl; // <-- LÍNEA AÑADIDA
    write(sockfd, header.c_str(), header.size());

    FILE* fp = fopen(filepath.c_str(), "rb");
    char buffer[1024];
    while (!feof(fp)) {
        size_t n = fread(buffer, 1, sizeof(buffer), fp);
        if (n > 0) {
            write(sockfd, buffer, n);
        }
    }
    fclose(fp);
    cout<<"[Contenido del archivo enviado por completo: "<<filename<<"]\n";
}

bool playTTT(int sockfd){
    string p = "P\n";
    write(sockfd, p.c_str(), p.size());
    myTTTRole = 'S';
    bool running = true;
    char buf[1024];
    while(running){
        memset(buf,0,sizeof(buf));
        int r = read(sockfd, buf, sizeof(buf)-1);
        if (r <= 0) { cout << "Servidor desconectado durante TTT.\n"; return false; }
        string msg(buf);
        // Procesar por líneas (pueden venir varias juntas)
        size_t start = 0;
        while (true){
            size_t nl = msg.find('\n', start);
            if (nl == string::npos) break;
            string line = msg.substr(start, nl-start);
            start = nl+1;

            if (line.rfind("ROLE",0)==0){
                if (line.size()>=6) myTTTRole = line[5];
                cout << "Rol TTT: " << myTTTRole << "\n";
            } 
            else if (!line.empty() && line[0]=='v'){
                string b = (line.size()>2)? line.substr(2) : "";
                if (b.size()==9) printTTTBoard(b);
            } 
            else if (line.rfind("T ",0)==0){
                char turn = (line.size()>=3)? line[2] : '?';
                cout << "Turno: " << turn << "\n";
                if (myTTTRole==turn){
                    int pos;
                    cout << "Tu jugada ("<<myTTTRole<<") [1-9]: ";
                    cin >> pos;
                    cin.ignore(numeric_limits<streamsize>::max(), '\n');
                    string m = string("J") + myTTTRole + to_string(pos);
                    write(sockfd, m.c_str(), m.size());
                    cout << " Enviando jugada al servidor ==> " << m << endl;
                }
            } 
            else if (line.rfind("V ",0)==0){
                if (line.size()>=3 && line[2]=='T') cout << "✔ Jugada válida\n";
                else cout << "Jugada inválida\n";
            } 
            else if (line.rfind("WIN",0)==0){
                char w = (line.size()>=5)? line[4] : '?';
                cout << "🏆 Ganó " << w << "\n";
                running = false;
            }
        }
        if (!running) break;
    }
    return true;
}

int main(void) {
    struct sockaddr_in serverAddr;
    int sockfd;
    sockfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd == -1) { perror("No se pudo crear el socket"); exit(EXIT_FAILURE); }

    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &serverAddr.sin_addr);
    connect(sockfd, (struct sockaddr*)&serverAddr, sizeof(serverAddr));

    cout << "Conectado al servidor en " << SERVER_IP << ":" << PORT << endl;
    registerNickname(sockfd);
    thread reader(readThread, sockfd);

    int opcion;
    string buf;
    showMenu();
    do {
        
        cin >> opcion;
        cin.ignore(numeric_limits<streamsize>::max(), '\n');
        
        if(opcion==1){
            char msg = 'l';
            write(sockfd, &msg, 1);
            cout<<" Enviando al servidor ==> "<<msg<<endl;
            this_thread::sleep_for(chrono::milliseconds(200));
            continue;
        }

        else if(opcion == 2){
            string send_to;
            cout<<"\nMensaje para: "; getline(cin, send_to);
            cout<<"\nEscribe el mensaje: "; getline(cin, buf);
            string msg = string("t") + formatLength(send_to.size(),2) + send_to + formatLength(buf.size(),3) + buf;
            write(sockfd, msg.c_str(), msg.size());
            cout<<" Enviando al servidor ==> "<<msg<<endl; // <-- LÍNEA AÑADIDA
            showMenu();
        }

        else if(opcion == 3){
            cout<<"\nMensaje para todos: "; getline(cin, buf);
            string msg = string("m") + formatLength(buf.size(),3) + buf;
            write(sockfd, msg.c_str(), msg.size());
            cout<<" Enviando al servidor ==> "<<msg<<endl; // <-- LÍNEA AÑADIDA
        } 
        
        else if(opcion == 4){
            sendFile(sockfd);
        } 
        
        else if (opcion == 5){
            cout << "\n🎮 Iniciando búsqueda de oponente..." << endl;
            char msg = 'P';
            write(sockfd, &msg, 1);
            cout << " Enviando al servidor ==> " << msg << endl;
            cout << "Enviando invitación a todos los usuarios conectados..." << endl;
            // No llamar playTTT() aquí - el juego se manejará por readThread
        } 

        else if(opcion == 6){
            /*
            string send_to;
            cout << "\nEnviar objeto a (nickname): ";
            getline(cin, send_to);

            // --- Crear y poblar el objeto sala ---
            sala sala_obj;
            sala_obj.n = 101;
            strcpy(sala_obj.str, "Sala de Reuniones A-30");
            sala_obj.c = new sala::cocina();
            sala_obj.c->cocinaNum = 1;
            strcpy(sala_obj.c->cocinaVector, "Area de Cafe");
            sala_obj.c->cocinaFLotante = _Float16(99.9f);

            // --- Serializar y enviar ---
            char command = 'o'; // <--- CAMBIO a 'o' minúscula
            string to_msg = formatLength(send_to.size(), 2) + send_to;

            cout << "Enviando al servidor ==> " << command << to_msg << "[...datos binarios...]" << endl; // <-- LÍNEA AÑADIDA

            write(sockfd, &command, 1);
            write(sockfd, to_msg.c_str(), to_msg.size());

            int len_str = strlen(sala_obj.str);
            int len_cocina_vec = strlen(sala_obj.c->cocinaVector);
            write(sockfd, &sala_obj.n, sizeof(sala_obj.n));
            write(sockfd, &len_str, sizeof(len_str));
            write(sockfd, sala_obj.str, len_str);
            write(sockfd, &sala_obj.c->cocinaNum, sizeof(sala_obj.c->cocinaNum));
            write(sockfd, &len_cocina_vec, sizeof(len_cocina_vec));
            write(sockfd, sala_obj.c->cocinaVector, len_cocina_vec);
            write(sockfd, &sala_obj.c->cocinaFLotante, sizeof(sala_obj.c->cocinaFLotante));
            
            delete sala_obj.c;
            */
        }

        else if (opcion == 7) {
            char msg = 'x';
            write(sockfd, &msg, 1);
            cout<<" Enviando al servidor ==> "<<msg<<endl; // <-- LÍNEA AÑADIDA
            break;
        }
    } while (true);

    shutdown(sockfd, SHUT_RDWR);
    close(sockfd);
    reader.join();
    return 0;
}
