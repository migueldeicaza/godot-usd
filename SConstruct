#!/usr/bin/env python

import os


def _expand(path):
    return os.path.abspath(os.path.expanduser(path))


godot_cpp_path = _expand(ARGUMENTS.get("godot_cpp_path", os.environ.get("GODOT_CPP_PATH", "/Users/miguel/cvs/Terrain3D/godot-cpp")))
usd_sdk_path = _expand(ARGUMENTS.get("usd_sdk_path", os.environ.get("USD_SDK_PATH", "/Users/miguel/cvs/usd/install")))
tbb_sdk_path = _expand(ARGUMENTS.get("tbb_sdk_path", os.environ.get("TBB_SDK_PATH", "/opt/homebrew/opt/tbb")))

if not os.path.isdir(godot_cpp_path):
    raise RuntimeError("godot-cpp checkout not found. Set GODOT_CPP_PATH or pass godot_cpp_path=/path/to/godot-cpp.")

if not os.path.isdir(os.path.join(usd_sdk_path, "include")) or not os.path.isdir(os.path.join(usd_sdk_path, "lib")):
    raise RuntimeError("OpenUSD SDK not found. Set USD_SDK_PATH to an install prefix with include/ and lib/.")

env = SConscript(os.path.join(godot_cpp_path, "SConstruct"))

env.Append(CPPPATH=["src"])
env.AppendUnique(CPPPATH=[os.path.join(usd_sdk_path, "include")])
env.AppendUnique(LIBPATH=[os.path.join(usd_sdk_path, "lib")])

if os.path.isdir(os.path.join(tbb_sdk_path, "include")):
    env.AppendUnique(CPPPATH=[os.path.join(tbb_sdk_path, "include")])

if os.path.isdir(os.path.join(tbb_sdk_path, "lib")):
    env.AppendUnique(LIBPATH=[os.path.join(tbb_sdk_path, "lib")])

env.AppendUnique(
    LIBS=[
        "usd_usdUtils",
        "usd_usdLux",
        "usd_usdShade",
        "usd_usdSkel",
        "usd_usdGeom",
        "usd_usd",
        "usd_sdf",
        "usd_tf",
        "usd_vt",
        "usd_gf",
        "usd_arch",
        "tbb",
    ]
)

if env["platform"] in ("linux", "macos"):
    env.AppendUnique(LINKFLAGS=[f"-Wl,-rpath,{os.path.join(usd_sdk_path, 'lib')}"])
    if os.path.isdir(os.path.join(tbb_sdk_path, "lib")):
        env.AppendUnique(LINKFLAGS=[f"-Wl,-rpath,{os.path.join(tbb_sdk_path, 'lib')}"])

sources = Glob("src/*.cpp")

library = env.SharedLibrary(
    "project/bin/libgodot_usd{}{}".format(env["suffix"], env["SHLIBSUFFIX"]),
    source=sources,
)

env.NoCache(library)
Default(library)
