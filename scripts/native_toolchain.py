"""Windows helper for the native test env.

Looks for a MinGW-w64 g++ (MSYS2) when it is not on PATH, and moves TEMP to an
ASCII-only directory because gcc cannot open its temp files under a profile
path with accented characters. Does nothing on other platforms or when the
paths do not exist.
"""
import os
import shutil

Import("env")  # noqa: F821

if os.name == "nt":
    if not shutil.which("g++", path=env["ENV"].get("PATH", "")):  # noqa: F821
        for candidate in (r"C:\msys64\ucrt64\bin", r"C:\msys64\mingw64\bin"):
            if os.path.isfile(os.path.join(candidate, "g++.exe")):
                env.PrependENVPath("PATH", candidate)  # noqa: F821
                break

    # Test binaries are launched by PlatformIO without the MinGW bin dir on
    # PATH, so link the runtime statically instead of needing its DLLs.
    env.Append(LINKFLAGS=["-static"])  # noqa: F821

    tmp = env["ENV"].get("TEMP", "")  # noqa: F821
    if not tmp.isascii():
        ascii_tmp = r"C:\pio_tmp"
        os.makedirs(ascii_tmp, exist_ok=True)
        env["ENV"]["TEMP"] = ascii_tmp  # noqa: F821
        env["ENV"]["TMP"] = ascii_tmp  # noqa: F821
