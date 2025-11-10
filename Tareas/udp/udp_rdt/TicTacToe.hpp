#ifndef TICTACTOE_HPP
#define TICTACTOE_HPP

#include <vector>
#include <unordered_set>
#include <map>
#include <string>
#include "extras.hpp"

// Helpers definidos en udpSer.cpp
bool reliableSendTo(UDP_Socket* server,
                    const std::vector<char>& payload,
                    const sockaddr_in& dest,
                    bool verbose = false);

void sendErrorReliable(UDP_Socket* server,
                       const sockaddr_in& dest,
                       const std::string& msg);

class TicTacToe {
private:
    std::vector<string> players;
    std::unordered_set<string> viewers;
    char board[9];
    int turn_index;
    bool game_active;

    bool checkWin(char sym);
    bool checkDraw();
    void sendBoard(UDP_Socket *udp, const sockaddr_in& dest);
    void broadcastBoard(UDP_Socket *udp, std::map<std::string, sockaddr_in>& users);
    void sendTurn(UDP_Socket *server,  std::map<std::string, sockaddr_in>& users);
    void resetBoard();

public:
    TicTacToe();

    void handleJoin(UDP_Socket *udp, const std::string nickname, std::map<std::string, sockaddr_in>& users);
    void handleMove(UDP_Socket *udp, char symbol, int pos, const std::string nickname, std::map<std::string, sockaddr_in>& users);
    void handleDisconnect(UDP_Socket *udp, std::map<std::string, sockaddr_in>& users, std::string nickname);
    
    // Métodos nuevos para gestión de invitaciones y espectadores
    bool isGameActive() const { return game_active; }
    std::vector<std::string> getActivePlayers() const { return players; }
    void addViewer(UDP_Socket *udp, const std::string nickname, std::map<std::string, sockaddr_in>& users);
};

#endif
