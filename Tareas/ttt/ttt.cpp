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
#include <set>
#include <array>
#include <mutex>
using namespace std;

//codigo para server.cpp
struct TicTacToeJuego {
    int playerO = -1;
    int playerX = -1;
    std::set<int> espectadores;
    std::array<char, 9> board{};
    char turn = 'O';
    bool active = false;
    bool waitingForOpponent = false;
    int challengerSock = -1;

    TicTacToeJuego() { resetearTablero(); }

    void resetearTablero() {
        for (size_t i = 0; i < board.size(); ++i) {
            board[i] = static_cast<char>('1' + i);
        }
        turn = 'O';
    }

    void resetearSesion() {
        playerO = -1;
        playerX = -1;
        challengerSock = -1;
        waitingForOpponent = false;
        active = false;
        resetearTablero();
        espectadores.clear();
    }
};

//variables server.cpp
mutex tttMutexServer;
TicTacToeJuego tttGame;



vector<int> reunirTTTDestinatariosBloqueados(const TicTacToeJuego& game) {
    vector<int> destinatarios;
    if (game.playerO != -1) {
        destinatarios.push_back(game.playerO);
    }
    if (game.playerX != -1 && game.playerX != game.playerO) {
        destinatarios.push_back(game.playerX);
    }
    destinatarios.insert(destinatarios.end(), game.espectadores.begin(), game.espectadores.end());
    return destinatarios;
}

bool tttVerificarGanar(const std::array<char, 9>& board, char p) {
    const int w[8][3] = {
        {0, 1, 2}, {3, 4, 5}, {6, 7, 8},
        {0, 3, 6}, {1, 4, 7}, {2, 5, 8},
        {0, 4, 8}, {2, 4, 6}
    };
    for (const auto& combo : w) {
        if (board[combo[0]] == p && board[combo[1]] == p && board[combo[2]] == p) {
            return true;
        }
    }
    return false;
}

bool tttTableroFull(const std::array<char, 9>& board) {
    for (char cell : board) {
        if (cell != 'O' && cell != 'X') {
            return false;
        }
    }
    return true;
}

string tttBoardString(const std::array<char, 9>& board) {
    return std::string(board.begin(), board.end());
}

void tttBroadcast(const vector<int>& destinatarios, const std::string& msg) {
    for (int sock : destinatarios) {
        write(sock, msg.c_str(), msg.size());
    }
}

//variables client.cpp
mutex tttMutexClient;
char tttRol = 'S';
char tttTurno = 'O';
string tttEstadoTablero = "123456789";
bool tttJuegoActivo = false;
bool tttHostEsperando = false;
bool tttInvitacionDisponible = false;
string tttInvitarNick;

//codigo para client.cpp
void printTTTtablero(const string& board) {
    cout << "\nTablero TTT:" << endl;
    for (size_t i = 0; i < board.size(); ++i) {
        char display = (board[i] == 'O' || board[i] == 'X') ? board[i] : static_cast<char>('-');
        cout << " " << display;
        if ((i + 1) % 3 == 0) {
            cout << "\n";
        } else {
            cout << " |";
        }
    }
}
