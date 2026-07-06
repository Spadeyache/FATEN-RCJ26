// Host test:  g++ -std=c++14 ColorAssign_test.cpp -o t && ./t
#include "ColorAssign.h"
#include <cstdio>

using namespace ColorAssign;
static const char* LN[] = {"SANG","RANG","SEGE","GARA"};

static void show(const char* name, const uint8_t c[4]) {
    Result r = compute(c);
    printf("%-28s -> layout=%-4s conf=%3.0f%%  numbers=[%u %u %u %u]\n",
           name, LN[r.layout], r.confidence * 100.0f,
           r.number[0], r.number[1], r.number[2], r.number[3]);
}

int main() {
    // faten{Green,Yellow} kavosh{Black} -> only SANG fits (certain)
    uint8_t a[4] = {CD_GREEN, CD_YELLOW, CD_BLACK, CD_NONE};
    show("SANG certain (G,Y,Bk)", a);

    // faten{Red,Green} kavosh{Orange} -> only RANG fits (Green+Red)
    uint8_t b[4] = {CD_RED, CD_GREEN, CD_ORANGE, CD_NONE};
    show("RANG certain (R,G,BO)", b);

    // one Silver forces SEGE
    uint8_t c[4] = {CD_SILVER, CD_YELLOW, CD_NONE, CD_NONE};
    show("SEGE via silver (Si,Y)", c);

    // two Silver -> SEGE, take BOTH silver columns (2 and 4)
    uint8_t d[4] = {CD_SILVER, CD_SILVER, CD_YELLOW, CD_BLUE};
    show("SEGE two silvers", d);

    // ambiguous: {Black, Orange} fits SANG/RANG/GARA -> GARA wins (two blacks)
    uint8_t e[4] = {CD_BLACK, CD_ORANGE, CD_NONE, CD_NONE};
    show("ambiguous (Bk,BO)", e);

    // two blacks -> GARA, take both black columns (2 and 4)
    uint8_t f[4] = {CD_BLACK, CD_BLACK, CD_RED, CD_BLUE};
    show("GARA two blacks", f);

    // infeasible: two Blue/Orange (no layout has 2 BO) -> degraded, conf 0
    uint8_t g[4] = {CD_BLUE, CD_ORANGE, CD_NONE, CD_NONE};
    show("infeasible (BO,BO)", g);
    return 0;
}
