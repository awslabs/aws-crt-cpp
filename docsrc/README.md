# Usage

- Pre-request: Make sure you have doxygen and python installed.

- Steps:
  - Change directory to docsrc/. Make sure you are under docsrc/ folder, since the Doxyfile is configured to use the relative path of this path.
  - run commands `python ./docs_gen_script.py`. The docs will be generated to docs/

Note: Check your doxygen version, if you have a different version with the one in docsrc/Doxyfile (First line in the file), update the Doxyfile is recommended. Also, check the generate html files from browser as a sanity check before you publish the documentation.
