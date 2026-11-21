#!/usr/bin/env python3
# alias tro ve tcp/test_hwclock.py
import os, sys
target = os.path.join(os.path.dirname(__file__), "tcp", "test_hwclock.py")
os.execv(sys.executable, [sys.executable, target] + sys.argv[1:])
