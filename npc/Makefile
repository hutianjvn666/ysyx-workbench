TOP_MODULE = example
TOP_FILE   = vsrc/example.v
CSRC_DIR   = csrc
CPP_FILE   = $(CSRC_DIR)/main.cpp

VERILATOR = verilator
VERILATOR_FLAGS = --cc --exe --build -j 0 -Wall --trace-fst
EXE = obj_dir/V$(TOP_MODULE)

all:
	@echo "Write this Makefile by your self."

sim: $(EXE)
	$(call git_commit, "sim RTL")
	./$(EXE)

$(EXE): $(TOP_FILE) $(CPP_FILE)
	$(VERILATOR) $(VERILATOR_FLAGS) --top-module $(TOP_MODULE) $(CPP_FILE) $(TOP_FILE)

clean:
	rm -rf obj_dir *.fst *.vcd

.PHONY: all sim clean

-include ../Makefile
