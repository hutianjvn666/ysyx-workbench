// WBU (write-back stage): select write-back data + compute next_pc
module wbu (
    input [ 6:0] opcode,
    input [ 4:0] rd,
    input [31:0] pc,
    input [31:0] alu_result,
    input [31:0] load_data,
    input [31:0] csr_rdata,     // CSR old value read by a CSR instruction
    input [31:0] imm_b,
    input [31:0] imm_j,
    input [31:0] mtvec,         // exception entry
    input [31:0] mepc,          // mret return address
    input        branch_taken,
    input        is_ebreak,
    input        is_ecall,
    input        is_mret,       // is it mret (exception return)
    input        is_csr,        // is it a CSR instruction (csrrw/csrrs/csrrc/...)

    output [ 4:0] waddr,
    output [31:0] wdata,
    output        wen,
    output [31:0] next_pc
);
  wire is_r_type = (opcode == 7'h33);
  wire is_i_alu = (opcode == 7'h13);
  wire is_load = (opcode == 7'h03);
  wire is_branch = (opcode == 7'h63);
  wire is_jal = (opcode == 7'h6f);
  wire is_jalr = (opcode == 7'h67);
  wire is_lui = (opcode == 7'h37);
  wire is_auipc = (opcode == 7'h17);

  // R/I-ALU/load/jal/jalr/lui/auipc/CSR write back; store/branch/ebreak/ecall/mret do not
  assign wen   = (is_r_type | is_i_alu | is_load | is_jal | is_jalr | is_lui | is_auipc | is_csr)
               & ~(is_ecall | is_mret | is_ebreak);
  assign waddr = rd;
  // jal/jalr write the link address (pc+4); load writes memory data; CSR writes the old value; others (add/sub/.../lui/auipc) write the ALU result
  assign wdata = is_csr            ? csr_rdata   :
                 (is_jal | is_jalr) ? (pc + 32'd4) :
                 is_load           ? load_data   : alu_result;
  // branch is conditional; jal/jalr unconditional (jalr clears LSB for alignment); else sequential
  assign next_pc = is_ecall  ? mtvec :
                   is_mret   ? mepc  :
                   is_ebreak ? pc :
                   is_branch ? (branch_taken ? (pc + imm_b) : (pc + 32'd4)) :
                   is_jal    ? (pc + imm_j) :
                   is_jalr   ? (alu_result & ~32'h1) :
                               (pc + 32'd4);
endmodule
