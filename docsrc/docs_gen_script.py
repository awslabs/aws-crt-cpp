import os

if os.system("doxygen ./Doxyfile") != 0:
    print("The 'doxygen' command was not found. Make sure you have Doxygen installed.")
    exit(1)
exit(0)
