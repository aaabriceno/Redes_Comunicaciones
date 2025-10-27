#include "TicTacToe.hpp"
#include "extras.hpp"
#include <iostream>
#include <algorithm>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
using namespace std;

TicTacToe::TicTacToe() {
    resetBoard();
}

void TicTacToe::resetBoard() {
    for (int i=0; i<9; i++) board[i] = '.';
    players.clear();
    viewers.clear();
    turn_index = 0;
    game_active = false;
}

bool TicTacToe::checkWin(char sym) {
    for (int i=0; i<9; i+=3) if (board[i]==sym && board[i+1]==sym && board[i+2]==sym) return true;
    for (int i=0; i<3; i++) if (board[i]==sym && board[i+3]==sym && board[i+6]==sym) return true;
    if (board[0]==sym && board[4]==sym && board[8]==sym) return true;
    if (board[2]==sym && board[4]==sym && board[6]==sym) return true;
    return false;
}

bool TicTacToe::checkDraw() {
    for (int i=0; i<9; i++) if (board[i]=='.') return false;
    return true;
}

void TicTacToe::sendBoard(UDP_Socket *server, const struct sockaddr_in *dest) {
    string board_s(board);
    string msg = "v" + board_s;
    vector<char> msg_send(msg.begin(), msg.end());
    udp_send(server, msg_send, dest);
    cout << msg << endl;
}

void TicTacToe::broadcastBoard(UDP_Socket *server, std::map<std::string, sockaddr_in>& users) {
    cout << "Players broadcast: " << endl;
    for (auto &p : players) sendBoard(server, &users[p]);
    cout << "Viewers broadcast: " << endl;
    for (auto &v : viewers) sendBoard(server, &users[v]);
}

void TicTacToe::sendTurn(UDP_Socket *server,  std::map<std::string, sockaddr_in>& users) {
    string nick = players[turn_index];
    char sym = (turn_index==0)?'O':'X';
    string msg = "V";
    msg.push_back(sym);
    vector<char> msg_send(msg.begin(), msg.end());
    udp_send(server, msg_send, &users[nick]);
    cout << msg << endl;
}

void TicTacToe::handleJoin(UDP_Socket *server, const std::string nickname, 
    std::map<std::string, sockaddr_in>& users) {

    if (!game_active) {
        resetBoard();
        game_active = true;
    }

    if (players.size() < 2) {
        if (find(players.begin(), players.end(), nickname) != players.end()) {
            sendError(server, &users[nickname], "You already are in the game.");
        } else {
            players.push_back(nickname);
            if (players.size()==2) {
                broadcastBoard(server, users);
                sendTurn(server, users);
            }
        }
    } else {
        viewers.insert(nickname);
        cout << "\n Joint a viewer("<< nickname <<")"<<endl;
        sendBoard(server, &users[nickname]);
    }
}

void TicTacToe::handleMove(UDP_Socket *server, char symbol, int pos, 
    const std::string nickname, std::map<std::string, sockaddr_in>& users){
        
    pos -= 1;
    if (pos>=0 && pos<9 && board[pos]=='.') {
        board[pos]=symbol;
        broadcastBoard(server, users);
        if (checkWin(symbol) || checkDraw()) {
            string winner = players[symbol!='O'];
            string loser  = players[symbol=='O'];
            string msg_w, msg_l, viewers_msg;
            
            if (checkDraw()) {
                string d = "Draw between "+ winner + " and " + loser;
                msg_w = string("o")+formatLength(d.size(),3)+ d;
                msg_l = msg_w;
                viewers_msg = msg_w;
            } else {
                string w = "You win " + winner, l = "You lose " + loser;
                msg_w = string("o")+formatLength(w.size(),3)+ w;
                msg_l = string("o")+formatLength(l.size(),3)+ l;

                viewers_msg = winner + " won against " + loser;
                viewers_msg = "o" + formatLength(viewers_msg.size(),3) + viewers_msg;
            }

            vector<char> msg_send(msg_w.begin(), msg_w.end());
            udp_send(server, msg_send, &users[players[symbol!='O']]);
            vector<char> msg_send_2(msg_l.begin(), msg_l.end());
            udp_send(server, msg_send_2, &users[players[symbol=='O']]);

            cout << msg_w << endl << msg_l << endl;

            for (string v : viewers) {
                vector<char> msg_send(viewers_msg.begin(), viewers_msg.end());
                udp_send(server, msg_send, &users[v]);
            }
            resetBoard();
        } else {
            turn_index = 1-turn_index;
            sendTurn(server, users);
        }
    } else {
        sendError(server, &users[nickname], "Enter a correct position pls.");
        sendTurn(server, users);
    }
}

void TicTacToe::addViewer(UDP_Socket *server, const std::string nickname, std::map<std::string, sockaddr_in>& users) {
    if (game_active && users.count(nickname)) {
        viewers.insert(nickname);
        
        // Enviar el tablero actual al nuevo espectador
        sendBoard(server, &users[nickname]);
        
        // Notificar que es espectador
        string msg = "o" + formatLength(37, 3) + "Eres espectador. ¡Disfruta el juego!";
        vector<char> msg_send(msg.begin(), msg.end());
        udp_send(server, msg_send, &users[nickname]);
        
        cout << "  👁️ '" << nickname << "' se unió como espectador\n";
    }
}

void TicTacToe::handleDisconnect(UDP_Socket *server, map<string, 
    sockaddr_in>& users, string nickname) {

    auto it = find(players.begin(), players.end(), nickname);
    if (it != players.end()) {
        players.erase(it);
        if (players.size()==1) {
            string reason = "You win!," + nickname + ", left server.";
            string msg = string("o")+formatLength(reason.size(),3)+ reason;
            vector<char> msg_send(msg.begin(), msg.end());
            udp_send(server, msg_send, &users[players[0]]);
            cout<<msg<<endl;
            resetBoard();
        }
    }
    viewers.erase(nickname);
}
