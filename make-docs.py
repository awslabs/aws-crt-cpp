#!/usr/bin/env python3
import os
import shutil

os.chdir(os.path.dirname(os.path.realpath(__file__)))

if shutil.which('doxygen') is None:
    exit("You must install doxygen")

# clean
if os.path.exists('docs'):
    shutil.rmtree('docs')

# build
os.chdir('docsrc')
if os.system('doxygen Doxyfile') != 0:
    exit(1)