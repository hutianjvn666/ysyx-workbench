// IDU (decode unit): extract instruction fields + 5 immediates (I/S/B/U/J) + ebreak/ecall/mret/CSR decode
module idu (
    input [31:0] inst,

    output [ 6:0] opcode,
    output [ 4:0] rd,
    output [ 4:0] rs1,
    output [ 4:0] rs2,
    output [ 2:0] funct3,
    output [ 6:0] funct7,
    output [31:0] imm_i,
    output [31:0] imm_s,
    output [31:0] imm_b,
    output [31:0] imm_u,
    output [31:0] imm_j,
    output        is_ebreak,
    output        is_ecall,   // 0x00000073
    output        is_mret,    // 0x30200073
    output        is_csr,
    output [ 1:0] csr_op,     // funct3[1:0]: 01=csrrw, 10=csrrs, 11=csrrc
    output        csr_imm,    // funct3[2]: 1 = immediate variant (zimm), 0 = register variant
    output [ 4:0] csr_zimm,   // zimm = inst[19:15]; also gates CSR write (rs1==0)
    output        csr_wen,    // write CSR: csrrw always; csrrs/csrrc only when rs1 != 0
    output [11:0] csr_addr    // CSR number = inst[31:20] (Zicsr instruction)
);
  assign opcode = inst[6:0];
  assign rd     = inst[11:7];
  assign funct3 = inst[14:12];
  assign rs1    = inst[19:15];
  assign rs2    = inst[24:20];
  assign funct7 = inst[31:25];

  assign imm_i  = {{20{inst[31]}}, inst[31:20]};
  assign imm_s  = {{20{inst[31]}}, inst[31:25], inst[11:7]};
  assign imm_b  = {{20{inst[31]}}, inst[7], inst[30:25], inst[11:8], 1'b0};
  assign imm_u  = {inst[31:12], 12'b0};
  assign imm_j  = {{12{inst[31]}}, inst[19:12], inst[20], inst[30:21], 1'b0};

  // SYSTEM instruction (opcode=0x73): funct3=000 is ecall/ebreak/mret, the rest are CSR instructions
  wire is_system = (opcode == 7'h73);
  // ebreak: imm[11:0]=0x001  (0x00100073)
  // ecall : imm[11:0]=0x000  (0x00000073)
  // mret  : imm[11:0]=0x302  (0x30200073)
  assign is_ebreak = is_system && (funct3 == 3'b000) && (inst[31:20] == 12'h001);
  assign is_ecall  = is_system && (funct3 == 3'b000) && (inst[31:20] == 12'h000);
  assign is_mret   = is_system && (funct3 == 3'b000) && (inst[31:20] == 12'h302);

  // CSR instruction: opcode=0x73, funct3 != 000
  assign is_csr = is_system && (funct3[1:0] != 2'b00);
  assign csr_op   = funct3[1:0];   // 01=csrrw/csrrwi, 10=csrrs/csrrsi, 11=csrrc/csrrci
  assign csr_imm  = funct3[2];     // 1 = immediate variant (zimm replaces the rs1 register)
  assign csr_zimm = inst[19:15];   // immediate variant: zimm; register variant: also the rs1 field
  assign csr_addr = inst[31:20];

  // write-CSR condition:
  //   csrrw/csrrwi: always write (csrw = csrrw x0,csr,rs must also write)
  //   csrrs/csrrc (+i): write only when the rs1/zimm field != 0 (csrr = csrrs rd,csr,x0 is read-only)
  assign csr_wen = is_csr && ((csr_op == 2'b01) || (csr_zimm != 5'b0));
endmodule
