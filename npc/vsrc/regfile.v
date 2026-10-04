// Register file: 32 x 32.
// RV32E ABI uses only x0~x15, but 32 entries match the 5-bit encoding
// (minirvEMU.c MAX_REGISTER is also 32); x16~x31 are unused by rv32e code.
// x0 reads as 0 (writing x0 is a dead write, so the write port is not gated).
module regfile #(
    parameter int ADDR_WIDTH = 5,
    parameter int DATA_WIDTH = 32
) (
    input clk,
    input [ADDR_WIDTH-1:0] raddr1,
    input [ADDR_WIDTH-1:0] raddr2,
    output [DATA_WIDTH-1:0] rdata1,
    output [DATA_WIDTH-1:0] rdata2,

    input [DATA_WIDTH-1:0] wdata,
    input [ADDR_WIDTH-1:0] waddr,
    input wen,
    // x10 (a0) = return value (simulator judges success); x15 (a5) = ecall cause (yield: -1), read by CSR
    output [DATA_WIDTH-1:0] a0,
    output [DATA_WIDTH-1:0] a5
);
  reg [DATA_WIDTH-1:0] rf[2**ADDR_WIDTH];

  // write port
  always @(posedge clk) begin
    if (wen) rf[waddr] <= wdata;
  end

  // read port: x0 reads as 0
  assign rdata1 = (raddr1 == 0) ? 0 : rf[raddr1];
  assign rdata2 = (raddr2 == 0) ? 0 : rf[raddr2];

  // exposed directly (no x0 special case)
  assign a0 = rf[10];
  assign a5 = rf[15];
endmodule
