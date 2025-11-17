#include "extras.hpp"
#include <fstream>
#include <vector>
#include <iostream>

int main(){
    std::ifstream file("main.txt", std::ios::binary);
    if(!file){ std::cerr << "no main.txt" << std::endl; return 1; }
    file.seekg(0, std::ios::end);
    size_t total = file.tellg();
    file.seekg(0, std::ios::beg);
    std::string from = "luis";
    std::string fname = "main.txt";
    std::vector<char> chunk(MAXLINE - 100);
    int pos=0;
    while(true){
        file.read(chunk.data(), chunk.size());
        std::streamsize readed = file.gcount();
        if(readed <=0) break;
        std::string payload = formatLength(from.size(),2) + from
            + formatLength(fname.size(),3) + fname
            + formatLength(total,10)
            + formatLength(pos,8);
        payload.append(chunk.data(), static_cast<size_t>(readed));
        if(file.peek()==EOF){
            if(payload.size() < MAXLINE){
                payload.append(MAXLINE - payload.size(), '#');
            }
        }
        receiveFile(payload);
        ++pos;
    }
    return 0;
}
