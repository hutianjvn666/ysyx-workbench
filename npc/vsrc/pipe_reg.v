// pipe_reg: 级间寄存器切片 (valid/ready 握手 + 一拍寄存)
// 这是异步总线的可复用基本单元, 对应 Chisel 的 RegEnable(left, left.fire)。
// 插在相邻两级之间, 就能把单周期 CPU 升级成流水线:
//
//   单周期:  StageConnect = 组合直通          (right.bits := left.bits)
//   流水线:  StageConnect = 插一个 pipe_reg   (right <> RegEnable(left, left.fire))
//
// 行为: 内部一个 1 深度 FIFO (用 full 标志表示是否持有有效数据)。
//   - 空 (full=0): in_ready=1 可接收, out_valid=0 无货可发
//   - 满 (full=1): in_ready=0 拒收,   out_valid=1 可发
//   fire_in  = in_valid  & in_ready  (上游写进)
//   fire_out = out_valid & out_ready (下游取走)
//   二者天然互斥 (空时不能发、满时不能收), 故每周期至多发生一次传输, 恒定 1 拍延迟。
module pipe_reg #(
  parameter WIDTH = 32
)(
  input               clk,
  input               rst,
  // 上游 (master) 侧
  input  [WIDTH-1:0]  in_bits,
  input               in_valid,
  output              in_ready,
  // 下游 (slave) 侧
  output [WIDTH-1:0]  out_bits,
  output              out_valid,
  input               out_ready
);
  reg [WIDTH-1:0] data;
  reg             full;

  assign in_ready  = ~full;
  assign out_valid = full;
  assign out_bits  = data;

  wire fire_in  = in_valid  & in_ready;
  wire fire_out = out_valid & out_ready;

  always @(posedge clk) begin
    if (rst) begin
      full <= 1'b0;
      data <= {WIDTH{1'b0}};
    end else if (fire_in) begin
      data <= in_bits;
      full <= 1'b1;
    end else if (fire_out) begin
      full <= 1'b0;
    end
  end
endmodule
