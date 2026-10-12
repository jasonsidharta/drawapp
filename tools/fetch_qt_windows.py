"""Download the official Qt 6.12.0 MinGW binaries and the matching MinGW-w64 15.1 toolchain.

Reads Qt's online repository index (download.qt.io), verifies each archive against the
SHA-1 published next to it, and extracts only the modules this project needs.
Usage: python tools/fetch_qt_windows.py [install_root]            (default: %USERPROFILE%\\Qt)
       python tools/fetch_qt_windows.py --android [install_root]  (Qt for Android arm64-v8a)
"""

import hashlib
import os
import shutil
import sys
import urllib.request
import xml.etree.ElementTree as ET

import py7zr

REPO = "https://download.qt.io/online/qtsdkrepository/windows_x86/desktop"
QT_VERSION = "6.12.0"
QT_PACKAGE_DIR = f"{REPO}/qt6_6120/qt6_6120_mingw"
QT_PACKAGE = "qt.qt6.6120.win64_mingw"
QT_MODULES = ("qtbase", "qtdeclarative", "qtsvg", "MinGW-w64", "d3dcompiler", "opengl32sw")
# Qt 6.12.0's MinGW binaries are built with GCC 15.1.0 (config_qtbase.summary), but the repository
# still lists the MinGW 13.1 tool and runtime. Use the matching MinGW-Builds 15.1.0 release instead.
MINGW_URL = ("https://github.com/niXman/mingw-builds-binaries/releases/download/15.1.0-rt_v12-rev0/"
             "x86_64-15.1.0-release-posix-seh-msvcrt-rt_v12-rev0.7z")
# Recorded on first download (2026-10-11); the release publishes no checksum.
MINGW_SHA256 = "ba447149a73cab43fdaf3352d1164f2bd5e96bc01ae6564fbaebd62f1b8eb435"
MINGW_RUNTIME = ("libstdc++-6.dll", "libgcc_s_seh-1.dll", "libwinpthread-1.dll")


def fetch(url: str) -> bytes:
    with urllib.request.urlopen(url, timeout=120) as response:
        return response.read()


def package_archives(repo_dir: str, package: str) -> tuple[str, list[str]]:
    index = fetch(f"{repo_dir}/Updates.xml")
    expected = fetch(f"{repo_dir}/Updates.xml.sha256").split()[0].decode()
    if hashlib.sha256(index).hexdigest() != expected:
        raise RuntimeError(f"Updates.xml checksum mismatch in {repo_dir}")
    for update in ET.fromstring(index).iter("PackageUpdate"):
        if update.findtext("Name") == package:
            version = update.findtext("Version") or ""
            archives = [a.strip() for a in (update.findtext("DownloadableArchives") or "").split(",") if a.strip()]
            return version, archives
    raise RuntimeError(f"{package} not found in {repo_dir}")


def download_verified(url: str, dest: str) -> None:
    expected = fetch(url + ".sha1").split()[0].decode()
    if os.path.exists(dest) and hashlib.sha1(open(dest, "rb").read()).hexdigest() == expected:
        return
    print(f"downloading {url.rsplit('/', 1)[-1]}", flush=True)
    data = fetch(url)
    if hashlib.sha1(data).hexdigest() != expected:
        raise RuntimeError(f"SHA-1 mismatch for {url}")
    with open(dest, "wb") as f:
        f.write(data)


def install(repo_dir: str, package: str, wanted, target: str, cache: str) -> None:
    version, archives = package_archives(repo_dir, package)
    for archive in archives:
        if wanted and not archive.startswith(wanted):
            continue
        local = os.path.join(cache, archive)
        download_verified(f"{repo_dir}/{package}/{version}{archive}", local)
        print(f"extracting {archive}", flush=True)
        # Runtime DLL archives (MinGW runtime, d3dcompiler, opengl32sw) have no folder; Qt's installer puts them in bin/.
        destination = os.path.join(target, "bin") if package == QT_PACKAGE and not archive.startswith("qt") else target
        with py7zr.SevenZipFile(local, "r") as z:
            z.extractall(destination)


ANDROID_PACKAGE_DIR = "https://download.qt.io/online/qtsdkrepository/all_os/android/qt6_6120/qt6_6120_arm64_v8a"
ANDROID_PACKAGE = "qt.qt6.6120.android_arm64_v8a"
ANDROID_MODULES = ("qtbase", "qtdeclarative", "qtsvg")


def install_android(root: str, cache: str) -> None:
    prefix = os.path.join(root, QT_VERSION, "android_arm64_v8a")
    os.makedirs(prefix, exist_ok=True)
    install(ANDROID_PACKAGE_DIR, ANDROID_PACKAGE, ANDROID_MODULES, prefix, cache)
    print(f"Qt for Android prefix: {prefix}")


def main() -> None:
    if "--android" in sys.argv:
        sys.argv.remove("--android")
        root = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.environ["USERPROFILE"], "Qt")
        cache = os.path.join(root, "_downloads")
        os.makedirs(cache, exist_ok=True)
        install_android(root, cache)
        return
    root = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.environ["USERPROFILE"], "Qt")
    cache = os.path.join(root, "_downloads")
    os.makedirs(cache, exist_ok=True)
    qt_prefix = os.path.join(root, QT_VERSION, "mingw_64")
    os.makedirs(qt_prefix, exist_ok=True)
    install(QT_PACKAGE_DIR, QT_PACKAGE, QT_MODULES, qt_prefix, cache)

    mingw = os.path.join(root, "Tools", "mingw1510_64")
    archive = os.path.join(cache, MINGW_URL.rsplit("/", 1)[-1])
    if not os.path.exists(archive):
        print("downloading MinGW-w64 15.1.0", flush=True)
        with open(archive, "wb") as f:
            f.write(fetch(MINGW_URL))
    if hashlib.sha256(open(archive, "rb").read()).hexdigest() != MINGW_SHA256:
        raise RuntimeError("MinGW archive checksum mismatch")
    if not os.path.isdir(mingw):
        staging = mingw + "_tmp"
        with py7zr.SevenZipFile(archive, "r") as z:
            z.extractall(staging)
        shutil.move(os.path.join(staging, "mingw64"), mingw)
        shutil.rmtree(staging)
    # Qt's bundled runtime DLLs are from 13.1; replace them so Qt runs on the runtime it was built with.
    for dll in MINGW_RUNTIME:
        shutil.copy2(os.path.join(mingw, "bin", dll), os.path.join(qt_prefix, "bin", dll))

    print(f"Qt prefix: {qt_prefix}")
    print(f"MinGW:     {mingw}")


if __name__ == "__main__":
    main()
