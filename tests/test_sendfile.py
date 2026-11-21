#!/usr/bin/env python3
# alias tro ve http/test_sendfile.py
import os, sys
target = os.path.join(os.path.dirname(__file__), "http", "test_sendfile.py")
os.execv(sys.executable, [sys.executable, target] + sys.argv[1:])
