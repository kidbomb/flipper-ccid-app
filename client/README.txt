# This file ensures the client directory (Python test scripts) 
# is not included in the Flipper Zero application build.
# The build system will skip directories without proper .fam files
# when they don't contain C source files to compile.

# Note: This directory contains Python test scripts for testing
# the CCID implementation from a host computer.
