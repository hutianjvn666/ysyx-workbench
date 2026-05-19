// Verilated -*- C++ -*-
// DESCRIPTION: Verilator output: Tracing implementation internals

#include "verilated_fst_c.h"
#include "Vexample__Syms.h"


VL_ATTR_COLD void Vexample___024root__trace_init_top(Vexample___024root* vlSelf, VerilatedFst* tracep) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vexample___024root__trace_init_top\n"); );
    Vexample__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    auto& vlSelfRef = std::ref(*vlSelf).get();
    // Body
    // Empty
}

VL_ATTR_COLD void Vexample___024root__trace_const_0(void* voidSelf, VerilatedFst::Buffer* bufp);
void Vexample___024root__trace_cleanup(void* voidSelf, VerilatedFst* /*unused*/);

VL_ATTR_COLD void Vexample___024root__trace_register(Vexample___024root* vlSelf, VerilatedFst* tracep) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vexample___024root__trace_register\n"); );
    Vexample__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    auto& vlSelfRef = std::ref(*vlSelf).get();
    // Body
    tracep->addConstCb(&Vexample___024root__trace_const_0, 0, vlSelf);
    tracep->addCleanupCb(&Vexample___024root__trace_cleanup, vlSelf);
}

VL_ATTR_COLD void Vexample___024root__trace_const_0(void* voidSelf, VerilatedFst::Buffer* bufp) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vexample___024root__trace_const_0\n"); );
    // Body
    Vexample___024root* const __restrict vlSelf VL_ATTR_UNUSED = static_cast<Vexample___024root*>(voidSelf);
    Vexample__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
}
