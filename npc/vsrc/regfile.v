// 寄存器堆: 32 x 32
// 注: RV32E 的 ABI 只用 x0~x15(16 个), 但寄存器堆做成 32 项与 5-bit 指令编码对齐,
//     参考 minirvEMU.c 的 MAX_REGISTER 也是 32; 多出的 x16~x31 不会被 rv32e 代码用到。
// x0 恒为 0 (读口处理; 写 x0 是死写, 读不回来, 故不挡写口)
module regfile #(parameter ADDR_WIDTH = 5, DATA_WIDTH = 32) (
  input  clk,
  // 读口
  input  [ADDR_WIDTH-1:0] raddr1,
  input  [ADDR_WIDTH-1:0] raddr2,
  output [DATA_WIDTH-1:0] rdata1,
  output [DATA_WIDTH-1:0] rdata2,
  // 写口
  input  [DATA_WIDTH-1:0] wdata,
  input  [ADDR_WIDTH-1:0] waddr,
  input  wen,
  // 调试观测: x10(a0) = 返回值/参数寄存器, 供仿真环境判断程序是否成功
  output [DATA_WIDTH-1:0] a0
);
  reg [DATA_WIDTH-1:0] rf [2**ADDR_WIDTH-1:0];

  // 写口
  always @(posedge clk) begin
    if (wen) rf[waddr] <= wdata;
  end

  // 读口: 0 号寄存器恒读 0 (x0 特性)
  assign rdata1 = (raddr1 == 0) ? 0 : rf[raddr1];
  assign rdata2 = (raddr2 == 0) ? 0 : rf[raddr2];

  // x10 直接暴露 (a0 不参与 x0 特判)
  assign a0 = rf[10];
endmodule
