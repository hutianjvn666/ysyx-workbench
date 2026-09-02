// WBU: 写回单元 —— 选择写回数据 + 计算 next_pc
// 总线视角: WBU 是三级链的终点 slave。
//   in_valid 来自 EXU, 用来门控写回 (输入消息无效时写回寄存器堆);
//   in_ready 回给 EXU, 作为终点恒为 1 (组合逻辑恒就绪)。
module wbu(
  input  [6:0]  opcode,
  input  [4:0]  rd,
  input  [31:0] pc,
  input  [31:0] alu_result,
  input  [31:0] load_data,
  input  [31:0] imm_b,
  input  [31:0] imm_j,
  input         branch_taken,
  input         is_ebreak,
  input         in_valid,     // 上游 EXU 的有效信号
  output [4:0]  waddr,
  output [31:0] wdata,
  output        wen,
  output [31:0] next_pc,
  output        in_ready      // 回给 EXU 的就绪信号 (终点恒 1)
);
  wire is_r_type = (opcode == 7'h33);
  wire is_i_alu  = (opcode == 7'h13);
  wire is_load   = (opcode == 7'h03);
  wire is_branch = (opcode == 7'h63);
  wire is_jal    = (opcode == 7'h6f);
  wire is_jalr   = (opcode == 7'h67);
  wire is_lui    = (opcode == 7'h37);
  wire is_auipc  = (opcode == 7'h17);

  // 写回使能: 输入消息有效时才写回 (单周期 in_valid 恒 1, 等价原逻辑)
  // R/I-ALU/load/jal/jalr/lui/auipc 都要写回 (store/branch/ebreak 不写)
  assign wen   = in_valid & (is_r_type | is_i_alu | is_load | is_jal | is_jalr | is_lui | is_auipc) & ~is_ebreak;
  assign waddr = rd;
  // jal/jalr 写链接地址(pc+4); load 写访存读回数据; 其余(add/sub/.../lui/auipc)写 ALU 结果
  assign wdata = (is_jal | is_jalr) ? (pc + 32'd4) :
                 is_load           ? load_data   : alu_result;
  // ebreak 保持 PC 不变(停机); branch 条件跳; jal/jalr 无条件跳(jalr 结果低位清零对齐); 否则顺序执行
  assign next_pc = is_ebreak ? pc :
                   is_branch ? (branch_taken ? (pc + imm_b) : (pc + 32'd4)) :
                   is_jal    ? (pc + imm_j) :
                   is_jalr   ? (alu_result & ~32'h1) :
                               (pc + 32'd4);

  // 终点就绪: WBU 是最后一级, 组合逻辑恒就绪
  assign in_ready = 1'b1;
endmodule
