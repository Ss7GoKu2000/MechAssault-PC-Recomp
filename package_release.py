"""Package the already-built Windows host and a game-data-free source snapshot."""
from pathlib import Path
import argparse
import zipfile

ROOT = Path(__file__).resolve().parent
BUILD = ROOT / "out" / "build" / "release"
PROJECT_FILES = (
    ".gitattributes", ".gitignore", "CMakeLists.txt", "CMakePresets.json", "README.md",
    "build.bat", "release.bat", "package_release.py", "Launch-MechAssault.cmd", "Launch-MechAssault.ps1",
)
RUNTIME_FILES = ("README.md", "Launch-MechAssault.cmd", "Launch-MechAssault.ps1")
COMPILER_FILES = ("clang.exe", "lld-link.exe", "xbe_recomp.exe", "MechAssault.lib", "LLVM-LICENSE.txt")
CRT_FILES = ("msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll")
EXCLUDED = {".git", ".vs", "__pycache__", "assets", "generated", "out", "work"}


def source_files(folder):
    for file in sorted(folder.rglob("*")):
        if file.is_file() and not EXCLUDED.intersection(file.relative_to(folder).parts):
            if file.suffix.lower() in {".xbe", ".iso", ".dll", ".exe", ".pdb", ".obj", ".zip"}:
                raise ValueError(f"Unexpected binary in source tree: {file}")
            yield file


def write_zip(destination, entries):
    missing = [str(file) for file, _ in entries if not file.is_file()]
    if missing:
        raise FileNotFoundError("\n".join(missing))
    temporary = destination.with_suffix(".zip.tmp")
    with zipfile.ZipFile(temporary, "w", zipfile.ZIP_DEFLATED, compresslevel=5) as archive:
        for file, name in entries:
            archive.write(file, name)
    with zipfile.ZipFile(temporary) as archive:
        if archive.testzip() is not None:
            raise ValueError(f"ZIP integrity check failed: {destination}")
    temporary.replace(destination)
    print(destination)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--crt", type=Path, required=True, help="Visual Studio x64 Microsoft.VC143.CRT directory")
    args = parser.parse_args()
    output = ROOT / "out" / "private-test"
    output.mkdir(parents=True, exist_ok=True)

    source = [(ROOT / name, name) for name in PROJECT_FILES]
    for directory in ("config", "src", "sdk"):
        source.extend((file, file.relative_to(ROOT).as_posix()) for file in source_files(ROOT / directory))

    windows = [(ROOT / name, name) for name in RUNTIME_FILES]
    windows.append((BUILD / "MechAssault.exe", "MechAssault.exe"))
    for name in ("LICENSE", "THIRD_PARTY.txt"):
        windows.append((ROOT / "sdk" / name, "SDK-" + name))
    windows.extend((args.crt / name, name) for name in CRT_FILES)
    windows.extend((args.crt / name, f"compiler/{name}") for name in CRT_FILES)
    compiler = BUILD / "compiler"
    windows.extend((compiler / name, f"compiler/{name}") for name in COMPILER_FILES)
    windows.append((ROOT / "config" / "game.toml", "compiler/config/game.toml"))
    for directory in ("include", "xdk"):
        windows.extend((file, file.relative_to(BUILD).as_posix()) for file in source_files(compiler / directory))

    write_zip(output / "MechAssault-Rev1-source.zip", source)
    write_zip(output / "MechAssault-Rev1-Windows-x64.zip", windows)


if __name__ == "__main__":
    main()
