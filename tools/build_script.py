"""Compile script/ into integratedbodyphysics.ts4script. Run with Python 3.7 (the game's).
Usage: python3.7 tools/build_script.py <output_dir>"""
import os
import py_compile
import sys
import tempfile
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE = os.path.join(ROOT, 'script')

if sys.version_info[:2] != (3, 7):
    sys.exit('Python 3.7 is required')
out = sys.argv[1]
os.makedirs(out, exist_ok=True)
with tempfile.TemporaryDirectory() as tmp, zipfile.ZipFile(os.path.join(out, 'integratedbodyphysics.ts4script'), 'w') as zf:
    for folder, _, files in os.walk(SOURCE):
        for name in files:
            if name.endswith('.py'):
                src = os.path.join(folder, name)
                rel = os.path.relpath(src, SOURCE)
                pyc = os.path.join(tmp, rel + 'c')
                os.makedirs(os.path.dirname(pyc), exist_ok=True)
                py_compile.compile(src, cfile=pyc, dfile=rel, doraise=True)
                zf.write(pyc, (rel + 'c').replace(os.sep, '/'))
print('ok')
