// LSU: load/store unit
module lsu (
    input  [ 6:0] opcode,
    input  [ 2:0] funct3,
    input  [31:0] addr,
    input  [31:0] store_data,
    output [31:0] load_data
);

  wire is_lb = (opcode == 7'h03) && (funct3 == 3'b000);
  wire is_lh = (opcode == 7'h03) && (funct3 == 3'b001);
  wire is_lw = (opcode == 7'h03) && (funct3 == 3'b010);
  wire is_lbu = (opcode == 7'h03) && (funct3 == 3'b100);
  wire is_lhu = (opcode == 7'h03) && (funct3 == 3'b101);
  wire is_sb = (opcode == 7'h23) && (funct3 == 3'b000);
  wire is_sh = (opcode == 7'h23) && (funct3 == 3'b001);
  wire is_sw = (opcode == 7'h23) && (funct3 == 3'b010);

  wire mem_ren = is_lb | is_lh | is_lw | is_lbu | is_lhu;
  wire mem_wen = is_sb | is_sh | is_sw;

  wire [7:0] wmask = is_sw ? 8'h0f : (is_sh ? 8'h03 : 8'h01);
  wire [31:0] wdata = is_sw ? store_data : (is_sh ? {2{store_data[15:0]}} : {4{store_data[7:0]}});

  reg [31:0] rdata;

  import "DPI-C" function int pmem_read(
    input int raddr
  );  // return 4 little-endian bytes at addr
  import "DPI-C" function void pmem_write(
    input int  waddr,
    input int  wdata,
    input byte wmask
  );  // write 4 bytes at addr by wmask
  always_comb begin
    if (mem_ren) rdata = pmem_read(addr);
    else rdata = 32'b0;
    if (mem_wen) pmem_write(addr, wdata, wmask);
  end

  assign load_data = is_lb ? {{24{rdata[7]}}, rdata[7:0]} :
      is_lbu ? {24'b0, rdata[7:0]} :
      is_lh ? {{16{rdata[15]}}, rdata[15:0]} :
      is_lhu ? {16'b0, rdata[15:0]} :
      rdata;
endmodule
