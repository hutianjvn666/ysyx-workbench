#!/usr/bin/env python3
import sys, re

# Parse vga.hex (Logisim "v3.0 hex words addressed" format)
words = []
with open('vga.hex') as f:
    lines = f.readlines()
for line in lines[1:]:
    line = line.strip()
    if not line: continue
    parts = line.split()
    # first token is "addr:" - skip
    for tok in parts[1:]:
        words.append(int(tok, 16))

def decode(instr):
    op = instr & 0x7F
    rd = (instr >> 7) & 0x1F
    f3 = (instr >> 12) & 0x7
    rs1 = (instr >> 15) & 0x1F
    rs2 = (instr >> 20) & 0x1F
    f7 = (instr >> 25) & 0x7F

    names = {
        0x37: 'LUI', 0x17: 'AUIPC',
        0x6F: 'JAL', 0x67: 'JALR',
        0x63: 'BRANCH', 0x03: 'LOAD', 0x23: 'STORE',
        0x13: 'OP-IMM', 0x33: 'OP',
        0x0F: 'FENCE', 0x73: 'SYSTEM'
    }
    base = names.get(op, f'UNK(0x{op:02x})')

    sub = ''
    if op == 0x13:
        m3 = {0:'ADDI',1:'SLLI',2:'SLTI',3:'SLTIU',4:'XORI',5:'SRLI/SRAI',6:'ORI',7:'ANDI'}
        sub = m3.get(f3, '?')
    elif op == 0x33:
        if f7 == 0x20 and f3 == 0: sub = 'SUB'
        elif f7 == 0x20 and f3 == 5: sub = 'SRA'
        else:
            m3 = {0:'ADD',1:'SLL',2:'SLT',3:'SLTU',4:'XOR',5:'SRL',6:'OR',7:'AND'}
            sub = m3.get(f3, '?')
    elif op == 0x63:
        m3 = {0:'BEQ',1:'BNE',4:'BLT',5:'BGE',6:'BLTU',7:'BGEU'}
        sub = m3.get(f3, '?')
    elif op == 0x03:
        m3 = {0:'LB',1:'LH',2:'LW',4:'LBU',5:'LHU'}
        sub = m3.get(f3, '?')
    elif op == 0x23:
        m3 = {0:'SB',1:'SH',2:'SW'}
        sub = m3.get(f3, '?')
    elif op == 0x73:
        if instr == 0x00100073: sub = 'EBREAK'
        elif instr == 0x00000073: sub = 'ECALL'
        else: sub = 'SYS?'

    return base, sub, rd, rs1, rs2, f3, f7

# Count instruction usage
counts = {}
for i, w in enumerate(words):
    b, s, *_ = decode(w)
    key = f'{b}/{s}' if s else b
    counts[key] = counts.get(key, 0) + 1

print("Instruction usage:")
for k, v in sorted(counts.items(), key=lambda x: -x[1]):
    print(f"  {k:30s} {v}")
print(f"\nTotal: {len(words)} instructions")

# Now show specifically which instructions are NOT implemented by minirvEMU.c
implemented = {
    'LUI': True,
    'JALR/': True,
    'LOAD/LW': True, 'LOAD/LBU': True,
    'STORE/SW': True, 'STORE/SB': True,
    'OP-IMM/ADDI': True,
    'OP/ADD': True,
    'SYSTEM/EBREAK': True,
}
print("\n*** NOT IMPLEMENTED in your minirvEMU.c: ***")
for k, v in sorted(counts.items(), key=lambda x: -x[1]):
    kk = k if '/' in k else k+'/'
    if kk not in implemented and not k.startswith('LUI'):
        if k != 'LUI':
            print(f"  {k:30s} {v} occurrences")
