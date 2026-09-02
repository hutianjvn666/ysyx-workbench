// LSU: 访存单元 —— lb/lh/lw/lbu/lhu/sb/sh/sw, 通过 DPI-C 访问 C++ 侧 pmem
// pmem_read 返回 addr 处连续的 4 字节 (小端); pmem_write 按写掩码 wmask 写入 addr 处连续 4 字节
module lsu(
  input  [6:0]  opcode,
  input  [2:0]  funct3,
  input  [31:0] addr,        // 访存地址 = rs1 + imm
  input  [31:0] store_data,  // rs2 的值 (store)
  output [31:0] load_data    // 读回的数据 (已按大小符号/零扩展)
);
  import "DPI-C" function int  pmem_read (input int raddr);
  import "DPI-C" function void pmem_write(input int waddr, input int wdata, input byte wmask);

  wire is_lb  = (opcode == 7'h03) && (funct3 == 3'b000);
  wire is_lh  = (opcode == 7'h03) && (funct3 == 3'b001);
  wire is_lw  = (opcode == 7'h03) && (funct3 == 3'b010);
  wire is_lbu = (opcode == 7'h03) && (funct3 == 3'b100);
  wire is_lhu = (opcode == 7'h03) && (funct3 == 3'b101);
  wire is_sb  = (opcode == 7'h23) && (funct3 == 3'b000);
  wire is_sh  = (opcode == 7'h23) && (funct3 == 3'b001);
  wire is_sw  = (opcode == 7'h23) && (funct3 == 3'b010);

  wire mem_ren = is_lb | is_lh | is_lw | is_lbu | is_lhu;
  wire mem_wen = is_sb | is_sh | is_sw;

  // store: 按大小选写掩码, 把要写的字节复制到对应字节通道 (sw 写 4 字节, sh 写 2, sb 写 1)
  wire [7:0]  wmask = is_sw ? 8'h0f : (is_sh ? 8'h03 : 8'h01);
  wire [31:0] wdata = is_sw ? store_data : (is_sh ? {2{store_data[15:0]}} : {4{store_data[7:0]}});

  reg [31:0] rdata;
  always @(*) begin
    if (mem_ren) rdata = pmem_read(addr);
    else         rdata = 32'b0;
    if (mem_wen) pmem_write(addr, wdata, wmask);
  end

  // load: 按大小符号/零扩展 (取 addr 处最低字节/半字, rdata[7:0]=addr 处字节, rdata[15:0]=addr 处半字)
  assign load_data =
    is_lb  ? {{24{rdata[7]}},  rdata[7:0]}  :   // 符号扩展字节
    is_lbu ? {24'b0,           rdata[7:0]}  :   // 零扩展字节
    is_lh  ? {{16{rdata[15]}}, rdata[15:0]} :   // 符号扩展半字
    is_lhu ? {16'b0,           rdata[15:0]} :   // 零扩展半字
             rdata;                                // lw
endmodule
