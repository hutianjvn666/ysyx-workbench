# ============================================================
#  NPC 处理器仿真 Makefile（标准开发版）
#   - 自动扫描 vsrc/ 下所有 .v 与 csrc/ 下所有 C/C++ 源文件
#   - 分离 --cc（生成 C++ 模型）与 --build（编译链接）两步
#   - --trace-fst 支持 fst 波形，make wave 用 gtkwave 查看
#   - 顶层模块名通过 TOP_MODULE 配置
# ============================================================

# ---- 可配置项 ----
# 开始写 CPU 时，把这里改成你自己的顶层模块名（惯例是 top）
TOP_MODULE = top

BUILD_DIR  = ./build
OBJ_DIR    = $(BUILD_DIR)/obj_dir
BIN        = $(BUILD_DIR)/V$(TOP_MODULE)

# ---- 源文件自动扫描 ----
VSRCS = $(shell find $(abspath ./vsrc) -name "*.v")
CSRCS = $(shell find $(abspath ./csrc) -name "*.c" -or -name "*.cc" -or -name "*.cpp" -or -name "*.C")

# ---- Verilator ----
VERILATOR       = verilator
VERILATOR_FLAGS = -Wall --trace-fst -j 0
# 仿真提速可选（会掩盖 X 传播，bring-up 阶段建议先不开）
# VERILATOR_FLAGS += --x-assign fast --x-initial fast --noassert

CXXFLAGS += -O3
LDFLAGS  +=

# ---- capstone (反汇编, 供 itrace 用) ----
# 库和头文件在 tools/capstone/ (从 NEMU 的 tools/capstone 复制)。
# -Wl,-rpath 让运行期动态链接器能找到 libcapstone.so.5
CAPSTONE = $(abspath ./tools/capstone)
CXXFLAGS += -I$(CAPSTONE)/include
LDFLAGS  += -L$(CAPSTONE) -lcapstone -Wl,-rpath,$(CAPSTONE)

# ---- difftest (dlopen/dlsym 动态链接 NEMU 的 .so) ----
LDFLAGS  += -ldl

# ---- 波形查看器 ----
WAVE_VIEWER ?= gtkwave
WAVE_FILE   ?= dump.fst   # 与 csrc/main.cpp 里 trace->open() 的文件名保持一致

# ---- 默认目标：构建 ----
all: $(BIN)

# ---- 第一步：--cc 生成 C++ 仿真模型 ----
$(OBJ_DIR)/V$(TOP_MODULE).mk: $(VSRCS) $(CSRCS)
	@mkdir -p $(OBJ_DIR)
	@$(VERILATOR) $(VERILATOR_FLAGS) --cc --exe --top-module $(TOP_MODULE) \
		$(VSRCS) $(CSRCS) --Mdir $(OBJ_DIR) -o $(abspath $(BIN)) \
		$(addprefix -CFLAGS , $(CXXFLAGS)) $(addprefix -LDFLAGS , $(LDFLAGS))
	@touch $@

# ---- 第二步：--build 编译链接 ----
$(BIN): $(OBJ_DIR)/V$(TOP_MODULE).mk
	@$(MAKE) -C $(OBJ_DIR) -f V$(TOP_MODULE).mk

# ---- 运行仿真 ----
sim: $(BIN)
	$(call git_commit, "sim RTL")
	@$(BIN)

# ---- 查看波形 ----
wave:
	$(WAVE_VIEWER) $(WAVE_FILE)

# ---- 清理 ----
clean:
	rm -rf $(BUILD_DIR) *.fst *.vcd *.vcd.fst

.PHONY: all sim wave clean

# 引入框架的 git_commit 追踪机制（勿删）
-include ../Makefile
