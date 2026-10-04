// Top level: instantiates IFU/IDU/EXU/LSU/WBU/REGFILE/CSR; memory via DPI-C (main.cpp).
module top (
    input         clk,
    input         rst,
    output [31:0] pc,
    output [31:0] inst,
    output [31:0] a0     // x10
);

  wire [31:0] next_pc;
  wire [ 6:0] opcode;
  wire [ 2:0] funct3;
  wire [ 6:0] funct7;
  wire [4:0] rd, rs1, rs2;
  wire [31:0] imm_i, imm_s, imm_b, imm_u, imm_j;
  wire [31:0] rdata1, rdata2;
  wire [31:0] alu_result;
  wire [31:0] load_data;
  wire [ 4:0] waddr;
  wire [31:0] wdata;
  wire        wen;
  wire        branch_taken;
  wire        is_ebreak;
  wire        is_ecall;
  wire        is_mret;
  wire        is_csr;
  wire [ 1:0] csr_op;
  wire        csr_imm;
  wire [ 4:0] csr_zimm;
  wire        csr_wen;
  wire [11:0] csr_addr;
  wire [31:0] csr_rdata;
  wire [31:0] mtvec, mepc;
  wire [31:0] a5;  // ecall cause (yield: -1)


  import "DPI-C" function void ebreak();
  always @(posedge clk) begin
    if (is_ebreak && !rst) ebreak();
  end

  ifu u_ifu (
      .clk      (clk),
      .rst      (rst),
      .next_pc  (next_pc),
      .pc       (pc),
      .inst     (inst)
  );

  idu u_idu (
      .inst     (inst),
      .opcode   (opcode),
      .rd       (rd),
      .rs1      (rs1),
      .rs2      (rs2),
      .funct3   (funct3),
      .funct7   (funct7),
      .imm_i    (imm_i),
      .imm_s    (imm_s),
      .imm_b    (imm_b),
      .imm_u    (imm_u),
      .imm_j    (imm_j),
      .is_ebreak(is_ebreak),
      .is_ecall (is_ecall),
      .is_mret  (is_mret),
      .is_csr   (is_csr),
      .csr_op   (csr_op),
      .csr_imm  (csr_imm),
      .csr_zimm (csr_zimm),
      .csr_wen  (csr_wen),
      .csr_addr (csr_addr)
  );

  regfile u_rf (
      .clk   (clk),
      .raddr1(rs1),
      .raddr2(rs2),
      .rdata1(rdata1),
      .rdata2(rdata2),
      .wdata (wdata),
      .waddr (waddr),
      .wen   (wen),
      .a0    (a0),
      .a5    (a5)
  );

  exu u_exu (
      .opcode      (opcode),
      .funct3      (funct3),
      .funct7      (funct7),
      .rdata1      (rdata1),
      .rdata2      (rdata2),
      .imm_i       (imm_i),
      .imm_s       (imm_s),
      .imm_u       (imm_u),
      .pc          (pc),
      .alu_result  (alu_result),
      .branch_taken(branch_taken)
  );

  lsu u_lsu (
      .opcode    (opcode),
      .funct3    (funct3),
      .addr      (alu_result),
      .store_data(rdata2),
      .load_data (load_data)
  );

  csr u_csr (
      .clk        (clk),
      .rst        (rst),
      .csr_addr   (csr_addr),
      .csr_op     (csr_op),
      .csr_imm    (csr_imm),
      .csr_zimm   (csr_zimm),
      .csr_rs1_val(rdata1),
      .csr_wen    (csr_wen),
      .is_ecall   (is_ecall),
      .is_mret    (is_mret),
      .pc         (pc),
      .ecall_cause(a5),
      .csr_rdata  (csr_rdata),
      .mtvec      (mtvec),
      .mepc       (mepc)
  );

  wbu u_wbu (
      .opcode      (opcode),
      .rd          (rd),
      .pc          (pc),
      .alu_result  (alu_result),
      .load_data   (load_data),
      .csr_rdata   (csr_rdata),
      .imm_b       (imm_b),
      .imm_j       (imm_j),
      .mtvec       (mtvec),
      .mepc        (mepc),
      .branch_taken(branch_taken),
      .is_ebreak   (is_ebreak),
      .is_ecall    (is_ecall),
      .is_mret     (is_mret),
      .is_csr      (is_csr),
      .waddr       (waddr),
      .wdata       (wdata),
      .wen         (wen),
      .next_pc     (next_pc)
  );
endmodule
