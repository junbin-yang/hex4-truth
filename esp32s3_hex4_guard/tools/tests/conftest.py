# -*- coding: utf-8 -*-
"""pytest 配置: 将 tools/ 加入 import 路径"""
import os
import sys

TOOLS_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
if TOOLS_DIR not in sys.path:
    sys.path.insert(0, TOOLS_DIR)

FIXTURES = os.path.join(os.path.dirname(__file__), "fixtures")
