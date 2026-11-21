#!/usr/bin/env python3
# alias tro ve http/test_http11.py
import os, sys
target = os.path.join(os.path.dirname(__file__), "http", "test_http11.py")
os.execv(sys.executable, [sys.executable, target] + sys.argv[1:])
