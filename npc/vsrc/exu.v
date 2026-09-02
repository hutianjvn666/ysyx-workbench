// EXU: 执行单元 —— 完整 ALU (R 型 + I 型) + 分支比较
// ALU 结果:
//   R 型 (0x33)        : rdata1 op rdata2 (add/sub/sll/slt/sltu/xor/srl/sra/or/and)
//   I 型 ALU (0x13)    : rdata1 op imm_i (addi/slti/sltiu/xori/ori/andi/slli/srli/srai)
//   load (0x03)        : rdata1 + imm_i   (访存地址)
//   store (0x23)       : rdata1 + imm_s   (访存地址)
//   jalr (0x67)        : rdata1 + imm_i   (跳转目标)
//   lui (0x37)         : imm_u
//   auipc (0x17)       : pc + imm_u
// 总线视角: EXU 与 LSU 合起来是"执行级", EXU 挂这一级的 valid/ready;
//   LSU 是纯组合功能单元 (读内存出 load_data), 不单独挂握手。
module exu(
  input  [6:0]  opcode,
  input  [2:0]  funct3,
  input  [6:0]  funct7,
  input  [31:0] rdata1,     // rs1 的值
  input  [31:0] rdata2,     // rs2 的值
  input  [31:0] imm_i,      // I 型立即数
  input  [31:0] imm_s,      // S 型立即数
  input  [31:0] imm_u,      // U 型立即数
  input  [31:0] pc,
  input         in_valid,     // 上游 IDU 的有效信号
  input         out_ready,    // 下游 WBU 的就绪信号
  output reg [31:0] alu_result,
  output reg        branch_taken,
  output        in_ready,     // 回给 IDU 的就绪信号 (= out_ready)
  output        out_valid     // 传给 WBU 的有效信号 (= in_valid)
);
  // 指令类型
  wire is_r_type = (opcode == 7'h33);
  wire is_i_alu  = (opcode == 7'h13);
  wire is_store  = (opcode == 7'h23);
  wire is_branch = (opcode == 7'h63);
  wire is_lui    = (opcode == 7'h37);
  wire is_auipc  = (opcode == 7'h17);

  // ALU 第一操作数: auipc 用 pc, lui 用 0 (0 + imm_u = imm_u), 其余用 rdata1
  wire [31:0] operand_a = is_auipc ? pc : (is_lui ? 32'b0 : rdata1);

  // ALU 第二操作数: R 型用 rdata2, store 用 imm_s, lui/auipc 用 imm_u, 其余(load/jalr/I-ALU)用 imm_i
  wire [31:0] operand_b = is_r_type          ? rdata2 :
                          is_store           ? imm_s  :
                          (is_lui | is_auipc) ? imm_u :
                                               imm_i;

  // 只有 R 型 / I-ALU 才用 funct3 选操作; 其它指令固定做加法
  wire is_alu = is_r_type | is_i_alu;
  wire is_sub = is_r_type && (funct3 == 3'b000) && (funct7 == 7'h20);   // R型 sub (funct7=0100000)
  wire is_sra = (funct3 == 3'b101) && (funct7 == 7'h20);                // R型 sra / I型 srai

  // 4-bit ALU 操作码
  reg [3:0] alu_op;
  always @(*) begin
    if (!is_alu) alu_op = 4'd0;   // 加法 (load/store/jalr/lui/auipc)
    else case (funct3)
      3'b000: alu_op = is_sub ? 4'd1 : 4'd0;   // sub / add
      3'b001: alu_op = 4'd2;                   // sll
      3'b010: alu_op = 4'd3;                   // slt
      3'b011: alu_op = 4'd4;                   // sltu
      3'b100: alu_op = 4'd5;                   // xor
      3'b101: alu_op = is_sra ? 4'd7 : 4'd6;   // sra / srl
      3'b110: alu_op = 4'd8;                   // or
      3'b111: alu_op = 4'd9;                   // and
      default: alu_op = 4'd0;
    endcase
  end

  // ALU 运算
  always @(*) begin
    case (alu_op)
      4'd0: alu_result = operand_a + operand_b;
      4'd1: alu_result = operand_a - operand_b;
      4'd2: alu_result = operand_a << operand_b[4:0];          // sll
      4'd3: alu_result = ($signed(operand_a) < $signed(operand_b)) ? 32'd1 : 32'd0; // slt
      4'd4: alu_result = (operand_a < operand_b) ? 32'd1 : 32'd0;                   // sltu
      4'd5: alu_result = operand_a ^ operand_b;
      4'd6: alu_result = operand_a >> operand_b[4:0];          // srl
      4'd7: alu_result = $signed(operand_a) >>> operand_b[4:0]; // sra (算术右移)
      4'd8: alu_result = operand_a | operand_b;
      4'd9: alu_result = operand_a & operand_b;
      default: alu_result = 32'd0;
    endcase
  end

  // 分支比较 (beq/bne/blt/bge/bltu/bgeu), 用 rdata1/rdata2 原始值
  wire signed [31:0] s_rdata1 = rdata1;
  wire signed [31:0] s_rdata2 = rdata2;
  wire b_eq  = (rdata1 == rdata2);
  wire b_lt  = (s_rdata1 < s_rdata2);   // 有符号
  wire b_ltu = (rdata1 < rdata2);       // 无符号

  always @(*) begin
    if (!is_branch) branch_taken = 1'b0;
    else case (funct3)
      3'b000: branch_taken = b_eq;    // beq
      3'b001: branch_taken = ~b_eq;   // bne
      3'b100: branch_taken = b_lt;    // blt
      3'b101: branch_taken = ~b_lt;   // bge
      3'b110: branch_taken = b_ltu;   // bltu
      3'b111: branch_taken = ~b_ltu;  // bgeu
      default: branch_taken = 1'b0;
    endcase
  end

  // 总线握手: 组合透传 (valid 向前, ready 向后)
  assign out_valid = in_valid;
  assign in_ready  = out_ready;
endmodule
