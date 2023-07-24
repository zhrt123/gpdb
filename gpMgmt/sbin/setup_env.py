import os, sys

def setup_env():
    print("setup env")
    sys.path.append(os.environ.get("GPHOME") + "/lib/python")
