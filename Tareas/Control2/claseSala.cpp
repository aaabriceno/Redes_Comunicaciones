#include <iostream>
using namespace std;

struct xp{
    int n;
    char str[1000];
    _Float16 f;
};

class sala{
public:
    class silla{
    public:
        int sillaNum;
        char sillaVector[1000];
        _Float16 sillaFlotante;
    };
    class mesa{
    public:
        int mesaNum;
        char mesaVector[1000];
        _Float16 mesaFlotante;
    };
    class cocina{
    public:
        int cocinaNum;
        char cocinaVector[1000];
        _Float16 cocinaFLotante;
    };
    
    cocina* c;
    int n;
    char str[1000];  
};
/*
int main(){
    sala grande;
    xp cosa = {25,"hola, esto es una prueba de redes y comunicaciones",_Float16(24.5f)};
    cout << cosa.n << " "<< cosa.str << " "<< static_cast<float>(cosa.f);
}
*/