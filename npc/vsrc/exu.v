// EXU: execute unit - full ALU (R-type + I-type) + branch comparison
// ALU result computation:
//   R-type (0x33)      : rdata1 op rdata2  (add/sub/sll/slt/sltu/xor/srl/sra/or/and)
//   I-type ALU (0x13)  : rdata1 op imm_i   (addi/slti/sltiu/xori/ori/andi/slli/srli/srai)
//   load (0x03)        : rdata1 + imm_i    (memory address)
//   store (0x23)       : rdata1 + imm_s    (memory address)
//   jalr (0x67)        : rdata1 + imm_i    (jump target)
//   lui (0x37)         : imm_u
//   auipc (0x17)       : pc + imm_u
module exu (
    input      [ 6:0] opcode,
    input      [ 2:0] funct3,
    input      [ 6:0] funct7,
    input      [31:0] rdata1,
    input      [31:0] rdata2,
    input      [31:0] imm_i,
    input      [31:0] imm_s,
    input      [31:0] imm_u,
    input      [31:0] pc,
    output reg [31:0] alu_result,
    output reg        branch_taken
);
  wire is_r_type = (opcode == 7'h33);
  wire is_i_alu = (opcode == 7'h13);
  wire is_store = (opcode == 7'h23);
  wire is_branch = (opcode == 7'h63);
  wire is_lui = (opcode == 7'h37);
  wire is_auipc = (opcode == 7'h17);

  wire [31:0] operand_a = is_auipc ? pc : (is_lui ? 32'b0 : rdata1);
  wire [31:0] operand_b = is_r_type           ? rdata2 :
                          is_store            ? imm_s  :
                          (is_lui | is_auipc) ? imm_u  :
                                               imm_i;

  wire is_alu = is_r_type | is_i_alu;
  wire is_sub = is_r_type && (funct3 == 3'b000) && (funct7 == 7'h20);
  wire is_sra = (funct3 == 3'b101) && (funct7 == 7'h20);

  reg [3:0] alu_op;
  always_comb begin
    if (!is_alu) alu_op = 4'd0;
    else
      case (funct3)
        3'b000:  alu_op = is_sub ? 4'd1 : 4'd0;
        3'b001:  alu_op = 4'd2;
        3'b010:  alu_op = 4'd3;
        3'b011:  alu_op = 4'd4;
        3'b100:  alu_op = 4'd5;
        3'b101:  alu_op = is_sra ? 4'd7 : 4'd6;
        3'b110:  alu_op = 4'd8;
        3'b111:  alu_op = 4'd9;
        default: alu_op = 4'd0;
      endcase
  end

  // shift amount uses rs2[4:0] (RV32 shifts by 0..31 per spec)
  always_comb begin
    case (alu_op)
      4'd0: alu_result = operand_a + operand_b;
      4'd1: alu_result = operand_a - operand_b;
      4'd2: alu_result = operand_a << operand_b[4:0];
      4'd3: alu_result = ($signed(operand_a) < $signed(operand_b)) ? 32'd1 : 32'd0;
      4'd4: alu_result = (operand_a < operand_b) ? 32'd1 : 32'd0;
      4'd5: alu_result = operand_a ^ operand_b;
      4'd6: alu_result = operand_a >> operand_b[4:0];
      4'd7: alu_result = $signed(operand_a) >>> operand_b[4:0];
      4'd8: alu_result = operand_a | operand_b;
      4'd9: alu_result = operand_a & operand_b;
      default: alu_result = 32'd0;
    endcase
  end

  wire signed [31:0] s_rdata1 = rdata1;
  wire signed [31:0] s_rdata2 = rdata2;
  wire b_eq = (rdata1 == rdata2);
  wire b_lt = (s_rdata1 < s_rdata2);
  wire b_ltu = (rdata1 < rdata2);

  always_comb begin
    if (!is_branch) branch_taken = 1'b0;
    else
      // funct3: 000=beq 001=bne 100=blt 101=bge 110=bltu 111=bgeu
      case (funct3)
        3'b000:  branch_taken = b_eq;
        3'b001:  branch_taken = ~b_eq;
        3'b100:  branch_taken = b_lt;
        3'b101:  branch_taken = ~b_lt;
        3'b110:  branch_taken = b_ltu;
        3'b111:  branch_taken = ~b_ltu;
        default: branch_taken = 1'b0;
      endcase
  end
endmodule
