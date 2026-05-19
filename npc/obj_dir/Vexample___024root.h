// Verilated -*- C++ -*-
// DESCRIPTION: Verilator output: Design internal header
// See Vexample.h for the primary calling header

#ifndef VERILATED_VEXAMPLE___024ROOT_H_
#define VERILATED_VEXAMPLE___024ROOT_H_  // guard

#include "verilated.h"


class Vexample__Syms;

class alignas(VL_CACHE_LINE_BYTES) Vexample___024root final {
  public:

    // INTERNAL VARIABLES
    Vexample__Syms* vlSymsp;
    const char* vlNamep;

    // CONSTRUCTORS
    Vexample___024root(Vexample__Syms* symsp, const char* namep);
    ~Vexample___024root();
    VL_UNCOPYABLE(Vexample___024root);

    // INTERNAL METHODS
    void __Vconfigure(bool first);
};


#endif  // guard
