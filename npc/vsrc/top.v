// 顶层: 例化 IFU/IDU/EXU/LSU/WBU/REGFILE
// 存储器由 C++ (main.cpp) 通过 DPI-C 提供 (pmem_read/pmem_write/pmem_ifetch), 不拉信号到顶层
//
// 总线视角 (C4): 三级流水边界 IFU->IDU->EXU->WBU 各带 valid/ready 握手。
//   单周期: valid 恒 1、ready 恒 1, 纯组合直通, 等价原来的裸连线。
//   升级流水线: 在每条边界插一个 pipe_reg, 把 valid/ready 接到 pipe_reg 上即可:
//     wire [32:0] ifu_msg = {ifu_valid, inst};        // 把 valid 和 payload 打包
//     pipe_reg #(.WIDTH(33)) r_ifu_idu(.clk(clk), .rst(rst),
//       .in_bits(ifu_msg), .in_valid(ifu_valid), .in_ready(ifu_ready),
//       .out_bits(idu_msg), .out_valid(idu_valid), .out_ready(idu_ready));
module top(
  input         clk,
  input         rst,
  output [31:0] pc,    // 当前 PC (供 C++ 观测)
  output [31:0] inst,  // 当前指令 (供 C++ 观测)
  output [31:0] a0     // x10, 程序返回值, 供仿真环境判断是否成功
);
  // DPI-C 导入: 由 C++ 侧 (main.cpp) 提供 extern "C" 实现
  import "DPI-C" function void ebreak();

  // ==================== 级间总线信号 ====================
  // 三级边界的 valid/ready 握手线 (payload 用下面的数据 wire)。
  // 单周期拓扑: valid 向前传, ready 向后传, 两端退化为恒 1。

  // --- 边界 1: IFU -> IDU (取指消息: inst) ---
  wire ifu_valid, ifu_ready;
  // --- 边界 2: IDU -> EXU (译码消息: 译码字段) ---
  wire idu_valid, idu_ready;
  // --- 边界 3: EXU -> WBU (执行消息: alu_result/branch_taken + LSU 的 load_data) ---
  wire exu_valid, exu_ready;

  wire [31:0] next_pc;
  wire [6:0]  opcode;
  wire [2:0]  funct3;
  wire [6:0]  funct7;
  wire [4:0]  rd, rs1, rs2;
  wire [31:0] imm_i, imm_s, imm_b, imm_u, imm_j;
  wire [31:0] rdata1, rdata2;
  wire [31:0] alu_result;
  wire [31:0] load_data;
  wire [4:0]  waddr;
  wire [31:0] wdata;
  wire        wen;
  wire        branch_taken;
  wire        is_ebreak;

  // 执行 ebreak 指令时, 调用 DPI-C 函数通知仿真环境结束仿真
  always @(*) begin
    if (is_ebreak) ebreak();
  end

  ifu u_ifu(
    .clk       (clk),
    .rst       (rst),
    .next_pc   (next_pc),
    .out_ready (ifu_ready),   // 边界 1: IDU -> IFU (ready 向后)
    .pc        (pc),
    .inst      (inst),
    .out_valid (ifu_valid)    // 边界 1: IFU -> IDU (valid 向前)
  );

  idu u_idu(
    .inst      (inst),
    .in_valid  (ifu_valid),   // 边界 1: 收 IFU 的 valid
    .out_ready (idu_ready),   // 边界 2: 收 EXU 的 ready
    .opcode    (opcode),
    .rd        (rd),
    .rs1       (rs1),
    .rs2       (rs2),
    .funct3    (funct3),
    .funct7    (funct7),
    .imm_i     (imm_i),
    .imm_s     (imm_s),
    .imm_b     (imm_b),
    .imm_u     (imm_u),
    .imm_j     (imm_j),
    .is_ebreak (is_ebreak),
    .in_ready  (ifu_ready),   // 边界 1: IDU -> IFU (ready 向后)
    .out_valid (idu_valid)    // 边界 2: IDU -> EXU (valid 向前)
  );

  regfile u_rf(
    .clk    (clk),
    .raddr1 (rs1),
    .raddr2 (rs2),
    .rdata1 (rdata1),
    .rdata2 (rdata2),
    .wdata  (wdata),
    .waddr  (waddr),
    .wen    (wen),
    .a0     (a0)
  );

  exu u_exu(
    .opcode       (opcode),
    .funct3       (funct3),
    .funct7       (funct7),
    .rdata1       (rdata1),
    .rdata2       (rdata2),
    .imm_i        (imm_i),
    .imm_s        (imm_s),
    .imm_u        (imm_u),
    .pc           (pc),
    .in_valid     (idu_valid),   // 边界 2: 收 IDU 的 valid
    .out_ready    (exu_ready),   // 边界 3: 收 WBU 的 ready
    .alu_result   (alu_result),
    .branch_taken (branch_taken),
    .in_ready     (idu_ready),   // 边界 2: EXU -> IDU (ready 向后)
    .out_valid    (exu_valid)    // 边界 3: EXU -> WBU (valid 向前)
  );

  lsu u_lsu(
    .opcode     (opcode),
    .funct3     (funct3),
    .addr       (alu_result),
    .store_data (rdata2),
    .load_data  (load_data)
  );

  wbu u_wbu(
    .opcode       (opcode),
    .rd           (rd),
    .pc           (pc),
    .alu_result   (alu_result),
    .load_data    (load_data),
    .imm_b        (imm_b),
    .imm_j        (imm_j),
    .branch_taken (branch_taken),
    .is_ebreak    (is_ebreak),
    .in_valid     (exu_valid),   // 边界 3: 收 EXU 的 valid (门控写回)
    .waddr        (waddr),
    .wdata        (wdata),
    .wen          (wen),
    .next_pc      (next_pc),
    .in_ready     (exu_ready)    // 边界 3: WBU -> EXU (ready 向后, 恒 1)
  );
endmodule
