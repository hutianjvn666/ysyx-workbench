// csr.v: Control and Status Register (CSR) file
// Writable CSRs: mstatus(0x300), mtvec(0x305), mepc(0x341), mcause(0x342),
//                mscratch(0x340), mie(0x304)
// Read-only CSRs: mcycle(0xB00)/mcycleh(0xB80), mvendorid(0xF11), marchid(0xF12);
//                 mip(0x344) is always 0 (no interrupt sources)
// Instruction semantics: csrrw=write, csrrs=set-read, csrrc=clear-read;
//   immediate variants use zimm (zero-extended 5-bit) instead of rs1.
module csr (
    input         clk,
    input         rst,
    // CSR instruction access
    input  [11:0] csr_addr,     // instruction CSR field (inst[31:20])
    input  [ 1:0] csr_op,       // 01=csrrw, 10=csrrs, 11=csrrc
    input         csr_imm,      // 1=immediate variant (zimm), 0=register variant
    input  [ 4:0] csr_zimm,     // zimm immediate (= inst[19:15])
    input  [31:0] csr_rs1_val,  // rs1 value (register variant)
    input         csr_wen,      // write CSR (already gated by rd/rs1 conditions)
    // Exception / return side effects
    input         is_ecall,     // ecall in this cycle
    input         is_mret,      // mret in this cycle
    input  [31:0] pc,           // current PC (for computing mepc)
    input  [31:0] ecall_cause,  // ecall cause (software convention: a5 == -1 means yield)
    output [31:0] csr_rdata,    // old CSR value read back into rd
    output [31:0] mtvec,
    output [31:0] mepc
);
  reg [31:0] r_mstatus;
  reg [31:0] r_mtvec;
  reg [31:0] r_mepc;
  reg [31:0] r_mcause;
  reg [31:0] r_mscratch;
  reg [31:0] r_mie;

  reg [63:0] mcycle;

  always @(posedge clk) begin
    if (rst) begin
      // MPP=11 (M-mode), MIE=0
      r_mstatus  <= 32'h1800;
      r_mtvec    <= 32'h0;
      r_mepc     <= 32'h0;
      r_mcause   <= 32'h0;
      r_mscratch <= 32'h0;
      r_mie      <= 32'h0;
      mcycle     <= 64'h0;
    end else begin
      mcycle <= mcycle + 64'd1;

      if (is_ecall) begin
        r_mepc    <= pc + 32'd4;
        r_mcause  <= ecall_cause;
        // MPIE <= MIE and MIE <= 0 (disable interrupts in the handler)
        r_mstatus <= {r_mstatus[31:8], r_mstatus[3], r_mstatus[6:4], 1'b0, r_mstatus[2:0]};
      end else if (is_mret) begin
        // MIE <= MPIE and MPIE <= 1 (restore interrupt enable after the handler)
        r_mstatus <= {r_mstatus[31:8], 1'b1, r_mstatus[6:4], r_mstatus[7], r_mstatus[2:0]};
      end else if (csr_wen) begin
        case (csr_addr)
          12'h300: r_mstatus <= csr_wdata;
          12'h305: r_mtvec <= csr_wdata;
          12'h341: r_mepc <= csr_wdata;
          12'h342: r_mcause <= csr_wdata;
          12'h340: r_mscratch <= csr_wdata;
          12'h304: r_mie <= csr_wdata;
          default: ;  // mcycle/mcycleh/mvendorid/marchid/mip are read-only
        endcase
      end
    end
  end

  // write value: operand (zimm or rs1 value) + operation (csrrw/csrrs/csrrc)
  wire [31:0] csr_opnd = csr_imm ? {27'b0, csr_zimm} : csr_rs1_val;
  wire [31:0] csr_wdata = (csr_op == 2'b01) ? csr_opnd :
                          (csr_op == 2'b10) ? (csr_rdata | csr_opnd) :
                          (csr_op == 2'b11) ? (csr_rdata & ~csr_opnd) : 32'b0;

  // read: return the old value selected by the CSR number
  // mvendorid(0xF11): "ysyx" as ASCII = 0x79737978
  // marchid  (0xF12): student id 26080239 = 0x018DF3EF
  assign csr_rdata = (csr_addr == 12'h300) ? r_mstatus     :
                     (csr_addr == 12'h305) ? r_mtvec       :
                     (csr_addr == 12'h341) ? r_mepc        :
                     (csr_addr == 12'h342) ? r_mcause      :
                     (csr_addr == 12'h340) ? r_mscratch    :
                     (csr_addr == 12'h304) ? r_mie         :
                     (csr_addr == 12'h344) ? 32'h0         :
                     (csr_addr == 12'hB00) ? mcycle[31:0]  :
                     (csr_addr == 12'hB80) ? mcycle[63:32] :
                     (csr_addr == 12'hF11) ? 32'h79737978  :
                     (csr_addr == 12'hF12) ? 32'h018DF3EF  :
                                             32'h0;

  assign mtvec = r_mtvec;
  assign mepc = r_mepc;
endmodule
