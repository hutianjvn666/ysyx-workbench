// IFU: fetch unit
module ifu (
    input             clk,
    input             rst,
    input      [31:0] next_pc,
    output reg [31:0] pc,
    output reg [31:0] inst
);

  always @(posedge clk) begin
    if (rst) pc <= 32'h80000000;  // program starts at 0x80000000
    else pc <= next_pc;
  end

  import "DPI-C" function int pmem_ifetch(input int raddr);
  always_comb begin
    inst = pmem_ifetch(pc);
  end
endmodule
