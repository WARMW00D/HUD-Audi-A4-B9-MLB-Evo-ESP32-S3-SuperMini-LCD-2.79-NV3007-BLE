#pragma once
#include <cstdint>
#include <cstdlib>
#include <string>
static uint32_t test_ms; static bool test_down;
inline uint32_t millis(){return test_ms;}
inline void delay(unsigned n){test_ms+=n;}
inline void pinMode(int,int){}
inline int digitalRead(int){return test_down?0:1;}
#define LOW 0
#define INPUT_PULLUP 2
struct SerialT {void println(const char*){} template<class... T> void printf(const char*,T...){} };
static SerialT Serial;
struct ESPT {bool restarted=false;void restart(){restarted=true;}};
static ESPT ESP;
