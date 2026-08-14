#############################################################################
# HEX4-Truth 仿真 Makefile (Icarus Verilog)
#
# Copyright (c) 2026 Junbin Yang.
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#############################################################################
#
# 用法 (从仓库根运行):
#   make -C hdl/sim test    # ternary_core_lite 单元测试 (279 项)
#   make -C hdl/sim clean   # 清理

RTL_DIR  = ../rtl
TB_DIR   = ../tb
OUT_DIR  = output_sim
IVERILOG = iverilog
VVP      = vvp
IVERILOG_FLAGS = -Wall -g2012

.PHONY: all test clean

all: test

#---- ternary_core_lite 单元测试 (279 项: 穷举 + TC吸收律 + 确定性 + 非法操作码) ----
test: $(OUT_DIR)
	@echo "============================================================"
	@echo "  ternary_core_lite 单元测试 (HEX4-Truth)"
	@echo "============================================================"
	$(IVERILOG) $(IVERILOG_FLAGS) -o $(OUT_DIR)/unit_test.vvp \
		-s ternary_core_lite_unit_tb \
		$(RTL_DIR)/core/ternary_core_lite.v \
		$(TB_DIR)/ternary_core_lite_unit_tb.v
	cd $(OUT_DIR) && $(VVP) unit_test.vvp

#---- 输出目录 ----
$(OUT_DIR):
	mkdir -p $(OUT_DIR)

#---- 清理 ----
clean:
	rm -rf $(OUT_DIR)
