#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build one distribution's package from its recipe under packaging/.

    build-recipe-package.py <distribution> <version>

Fills the recipe template for that distribution from `debian/control`, builds
from a source archive of the checkout, and leaves the package in
`build/artifacts/`. Run it in the matching container under `containers/`; it
needs that distribution's package manager and its KWin development files.
"""

import argparse
import os
import platform
import shutil
import subprocess
import tarfile
import tempfile
from pathlib import Path

from distribution_packages import translate
from release_assets import validate_version

NAME = "kwin-effect-upscale"
# "kwin 6.7.5-1": the name pacman was asked about, and the version wanted.
PACMAN_QUERY_FIELDS = 2
# Targets whose package is built from a recipe under packaging/. Debian is not
# here: it has its own debian/ directory and dpkg-buildpackage.
RECIPES = {
    "fedora": Path("packaging/rpm/kwin-effect-upscale.spec.in"),
    "opensuse": Path("packaging/rpm/kwin-effect-upscale.spec.in"),
    "arch": Path("packaging/arch/PKGBUILD.in"),
    "freebsd": Path("packaging/freebsd/manifest.ucl.in"),
}


def installed_kwin_version(distribution: str) -> str:
    """Ask the package manager which KWin the package will be built against."""
    queries = {
        "fedora": ("rpm", "-q", "--queryformat", "%{VERSION}-%{RELEASE}", "kwin"),
        "opensuse": ("rpm", "-q", "--queryformat", "%{VERSION}-%{RELEASE}", "kwin6"),
        # Not --quiet, which prints the name and omits the version.
        "arch": ("pacman", "-Q", "kwin"),
        "freebsd": ("pkg", "query", "%v", "plasma6-kwin"),
    }
    output = subprocess.check_output(queries[distribution], text=True).strip()
    if distribution == "arch":
        # pacman -Q prints "kwin 6.7.5-1"; the dependency wants the version.
        parts = output.split()
        if len(parts) != PACMAN_QUERY_FIELDS:
            message = f"Cannot read the installed KWin version from {output!r}"
            raise ValueError(message)
        output = parts[1]
    if not output:
        message = f"No installed KWin to build against on {distribution}"
        raise ValueError(message)
    return output


def source_archive(root: Path, destination: Path, version: str) -> Path:
    """Pack the tracked files, so a build never picks up an untracked stray."""
    listed = subprocess.check_output(["git", "ls-files", "-z"], cwd=root)
    names = [os.fsdecode(entry) for entry in listed.split(b"\0") if entry]
    archive = destination / f"{NAME}-{version}.tar.gz"
    with tarfile.open(archive, "w:gz") as tar:
        for name in names:
            tar.add(root / name, arcname=f"{NAME}-{version}/{name}")
    return archive


def recipe(root: Path, distribution: str, version: str, kwin: str) -> str:
    """Fill the template, keeping debian/control the only dependency list."""
    text = (root / RECIPES[distribution]).read_text()
    packages = translate((root / "debian" / "control").read_text(), distribution)
    if distribution == "arch":
        rendered = "makedepends=(" + " ".join(f"'{name}'" for name in packages) + ")"
    else:
        rendered = "\n".join(f"BuildRequires:  {name}" for name in packages)
    for placeholder, value in (
        ("@BUILD_REQUIRES@", rendered),
        ("@MAKEDEPENDS@", rendered),
        ("@VERSION@", version),
        ("@KWIN_VERSION@", kwin),
        ("@KWIN_ORIGIN@", kwin_origin(distribution)),
    ):
        text = text.replace(placeholder, value)
    return text


def kwin_origin(distribution: str) -> str:
    """Read the ports origin pkg records for a dependency; FreeBSD alone has one."""
    if distribution != "freebsd":
        return ""
    return subprocess.check_output(["pkg", "query", "%o", "plasma6-kwin"], text=True).strip()


def build_rpm(root: Path, work: Path, distribution: str, version: str, kwin: str) -> list[Path]:
    """Build through rpmbuild, which wants its own directory layout.

    -ba rather than -bb, so the source package is built as well. An RPM
    distribution expects to be handed the SRPM a binary came from: it is what
    lets anyone rebuild it, and what the debuginfo and debugsource packages
    refer back to.
    """
    for directory in ("SOURCES", "SPECS", "BUILD", "RPMS", "SRPMS", "BUILDROOT"):
        (work / directory).mkdir(parents=True, exist_ok=True)
    source_archive(root, work / "SOURCES", version)
    spec = work / "SPECS" / f"{NAME}.spec"
    spec.write_text(recipe(root, distribution, version, kwin))
    subprocess.run(
        ["rpmbuild", "-ba", "--define", f"_topdir {work}", str(spec)],
        check=True,
    )
    return sorted([*work.glob("RPMS/*/*.rpm"), *work.glob("SRPMS/*.src.rpm")])


ARCH_BUILDER = "builder"


def build_arch(root: Path, version: str, kwin: str) -> list[Path]:
    """Build through makepkg as the image's unprivileged build user.

    Inside the container, not under the mounted checkout. makepkg refuses to
    run as root, and the unprivileged user it has to run as cannot write to a
    mount owned by whoever started the container. So it builds where it does
    own the directory, and only the finished package is copied back.
    """
    work = Path(tempfile.mkdtemp(prefix="upscale-arch-"))
    source_archive(root, work, version)
    (work / "PKGBUILD").write_text(recipe(root, "arch", version, kwin))
    run_as = []
    if os.geteuid() == 0:
        owner = f"{ARCH_BUILDER}:{ARCH_BUILDER}"
        subprocess.run(["chown", "-R", owner, str(work)], check=True)
        run_as = ["setpriv", "--reuid", ARCH_BUILDER, "--regid", ARCH_BUILDER, "--init-groups"]
    # makepkg needs a writable home of its own for its temporary files.
    makepkg = [*run_as, "env", f"HOME={work}", "makepkg", "--noconfirm", "--skipinteg"]
    subprocess.run([*makepkg, "--nodeps"], cwd=work, check=True)
    # And the source package, which on Arch is the recipe together with what it
    # builds from. --allsource keeps the tarball inside it, so the result stands
    # on its own rather than pointing at a file only this build had.
    subprocess.run([*makepkg, "--allsource"], cwd=work, check=True)
    return sorted([*work.glob("*.pkg.tar.zst"), *work.glob("*.src.tar.gz")])


def build_pkg(root: Path, work: Path, version: str, kwin: str) -> list[Path]:
    """Build a FreeBSD package with pkg create from a staged installation.

    pkg create wants three things that no other target here needs: a manifest
    describing the package, a packing list of the files it contains, and a
    staging directory holding them. The list is generated from the staging
    directory rather than written by hand, because a file installed but not
    listed is silently left out of the package.
    """
    if work.exists():
        shutil.rmtree(work)
    stage = work / "stage"
    stage.mkdir(parents=True)
    build = work / "build"
    subprocess.run(
        [
            "cmake",
            "-S",
            str(root),
            "-B",
            str(build),
            "-G",
            "Ninja",
            "-DCMAKE_INSTALL_PREFIX=/usr/local",
            "-DCMAKE_BUILD_TYPE=RelWithDebInfo",
            # Warnings are not errors for whoever is building the package; the
            # check jobs are where a warning is the author's to answer for.
            "-DCMAKE_COMPILE_WARNING_AS_ERROR=OFF",
            "-DBUILD_TESTING=OFF",
            f"-DUPSCALE_PACKAGE_VERSION={version}",
        ],
        check=True,
    )
    subprocess.run(["cmake", "--build", str(build)], check=True)
    subprocess.run(
        ["cmake", "--install", str(build)],
        check=True,
        env={**os.environ, "DESTDIR": str(stage)},
    )

    prefix = stage / "usr" / "local"
    listed = sorted(path for path in prefix.rglob("*") if path.is_file())
    if not listed:
        message = "The staged installation is empty; nothing would be packaged"
        raise ValueError(message)
    plist = work / "pkg-plist"
    plist.write_text("".join(f"{path.relative_to(prefix)}\n" for path in listed))
    manifest = work / "+MANIFEST"
    manifest.write_text(recipe(root, "freebsd", version, kwin))
    subprocess.run(
        [
            "pkg",
            "create",
            "-M",
            str(manifest),
            "-p",
            str(plist),
            "-r",
            str(prefix),
            "-o",
            str(work),
        ],
        check=True,
    )
    created = sorted(work.glob("*.pkg"))
    if len(created) != 1:
        message = f"pkg create produced {[path.name for path in created]}, expected one package"
        raise ValueError(message)
    # pkg names a package after its manifest, because a pkg repository is per
    # architecture and has no need to say so. Release assets all sit in one
    # place, so the architecture goes into the name here as it does everywhere
    # else. The file name is not part of what pkg add reads.
    named = created[0].with_name(f"{NAME}-{version}-{platform.machine()}.pkg")
    created[0].rename(named)
    return [named]


def main() -> None:
    """Build one distribution's package and collect it under build/artifacts."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("distribution", choices=sorted(RECIPES))
    parser.add_argument("version")
    arguments = parser.parse_args()
    version = validate_version(arguments.version)
    root = Path(__file__).resolve().parents[1]
    os.chdir(root)
    kwin = installed_kwin_version(arguments.distribution)
    print(f"Building for {arguments.distribution} against KWin {kwin}", flush=True)

    if arguments.distribution == "arch":
        packages = build_arch(root, version, kwin)
    elif arguments.distribution == "freebsd":
        packages = build_pkg(root, root / "build" / "packages" / "freebsd", version, kwin)
    else:
        work = root / "build" / "packages" / arguments.distribution
        if work.exists():
            shutil.rmtree(work)
        work.mkdir(parents=True)
        packages = build_rpm(root, work, arguments.distribution, version, kwin)
    if not packages:
        message = f"the build produced no package for {arguments.distribution}"
        raise ValueError(message)

    artifacts = root / "build" / "artifacts"
    artifacts.mkdir(parents=True, exist_ok=True)
    for package in packages:
        shutil.copy2(package, artifacts / package.name)
        print(f"  {package.name}", flush=True)


if __name__ == "__main__":
    main()
