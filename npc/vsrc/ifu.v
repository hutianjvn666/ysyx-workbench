// IFU: 取指单元 —— PC 寄存器 + 通过 DPI-C 从存储器取指
// 总线视角: IFU 是取指消息的 master, 主动驱动 out_valid + inst(payload)。
//   单周期: out_valid 恒为 1 (每拍都有有效指令待发送);
//   多周期: 它会变成 idle/wait_ready 状态机 (见 C4 讲义)。
//   out_ready 来自下游 IDU; 握手 (out_valid & out_ready) 成立时 PC 才前进,
//   否则保持 PC 不变 = 暂存要发送的指令, 防止信息丢失。
module ifu(
  input         clk,
  input         rst,
  input  [31:0] next_pc,
  input         out_ready,       // 下游 IDU 的就绪信号 (slave -> master)
  output reg [31:0] pc,          // 当前 PC (供 C++ 观测)
  output reg [31:0] inst,        // 取指消息的 payload
  output        out_valid        // 取指消息的有效信号 (单周期恒 1)
);
  import "DPI-C" function int pmem_ifetch(input int raddr);

  // 单周期: 每拍都有有效指令待发送 (多周期下改为状态机驱动)
  assign out_valid = 1'b1;

  // 握手成功 (valid & ready) 才更新 PC; 否则 PC 保持, 相当于暂存当前指令
  always @(posedge clk) begin
    if (rst) pc <= 32'h80000000;   // minirv-npc 约定: 程序从 0x80000000 开始
    else if (out_valid & out_ready) pc <= next_pc;
  end

  // 组合逻辑取指: 从 C++ 侧的 pmem 读出 pc 处指令
  always @(*) begin
    inst = pmem_ifetch(pc);
  end
endmodule
