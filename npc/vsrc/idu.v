// IDU: 译码单元 —— 提取指令字段 + 五种立即数 (I/S/B/U/J) + ebreak 译码
// 总线视角: IDU 是纯组合的 slave+master, 无内部状态, 所以:
//   out_valid = in_valid  (有效信号向前传)
//   in_ready  = out_ready (就绪信号向后传)
// 这正是组合模块的 "right <> left" 透传。
module idu(
  input  [31:0] inst,
  input         in_valid,     // 上游 IFU 的有效信号
  input         out_ready,    // 下游 EXU 的就绪信号
  output [6:0]  opcode,
  output [4:0]  rd,
  output [4:0]  rs1,
  output [4:0]  rs2,
  output [2:0]  funct3,
  output [6:0]  funct7,
  output [31:0] imm_i,     // I 型立即数: addi/jalr/lw/...
  output [31:0] imm_s,     // S 型立即数: sw/sh/sb
  output [31:0] imm_b,     // B 型立即数: beq/bne/blt/...
  output [31:0] imm_u,     // U 型立即数: lui/auipc
  output [31:0] imm_j,     // J 型立即数: jal
  output        is_ebreak,
  output        in_ready,     // 回给 IFU 的就绪信号 (= out_ready)
  output        out_valid     // 传给 EXU 的有效信号 (= in_valid)
);
  assign opcode   = inst[6:0];
  assign rd       = inst[11:7];
  assign funct3   = inst[14:12];
  assign rs1      = inst[19:15];
  assign rs2      = inst[24:20];
  assign funct7   = inst[31:25];

  assign imm_i = {{20{inst[31]}}, inst[31:20]};
  assign imm_s = {{20{inst[31]}}, inst[31:25], inst[11:7]};
  assign imm_b = {{20{inst[31]}}, inst[7], inst[30:25], inst[11:8], 1'b0};
  assign imm_u = {inst[31:12], 12'b0};
  assign imm_j = {{12{inst[31]}}, inst[19:12], inst[20], inst[30:21], 1'b0};

  // ebreak: opcode=SYSTEM(0x73), funct3=000, imm[11:0]=0x001  (0x00100073)
  assign is_ebreak = (opcode == 7'h73) && (funct3 == 3'b000) && (inst[31:20] == 12'h001);

  // 总线握手: 组合透传 (valid 向前, ready 向后)
  assign out_valid = in_valid;
  assign in_ready  = out_ready;
endmodule
