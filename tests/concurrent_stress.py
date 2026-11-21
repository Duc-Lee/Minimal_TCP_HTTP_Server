#!/usr/bin/env python3
# alias tro ve tcp/test_stress.py
import os, sys
target = os.path.join(os.path.dirname(__file__), "tcp", "test_stress.py")
os.execv(sys.executable, [sys.executable, target] + sys.argv[1:])
