#!/usr/bin/env python3
"""
release-firmware.py
Atomic Holly firmware release: extract FW_VERSION from .ino, rename .bin,
create GitHub release, update manifest.json. Prevents the "manifest
version != binary version" bootloop class of bug.

Usage:
    python release-firmware.py <path-to-firmware.ino> <path-to-compiled.bin>

What it does, in order:
    1. Extracts FW_VERSION constant from the .ino source.
    2. Validates format YYYY.MM.DD.NNNN.
    3. Renames .bin to Holly_RDiQ5000_<version>.bin (so the filename
       matches the embedded version stamp).
    4. Creates a GitHub release tagged with the build number (e.g. 5090).
    5. Uploads the binary as a release asset.
    6. Rewrites manifest.json so its `version` field MATCHES the binary's
       embedded FW_VERSION exactly. No more drift.
    7. Verifies the release URL is reachable.
    8. Optionally commits + pushes the manifest change.

Requirements:
    - Python 3.6+
    - gh CLI: https://cli.github.com  (authenticated to the repo)
    - git    (if you want the script to commit/push the manifest)
    - curl   (used implicitly by urllib for the reachability check)

Run from the root of the repo (so manifest.json is in the cwd).
"""

import sys
import os
import re
import json
import shutil
import subprocess
import urllib.request
import urllib.error


def err(msg):
    print(f"\033[31mERROR:\033[0m {msg}", file=sys.stderr)


def warn(msg):
    print(f"\033[33mWARN:\033[0m  {msg}", file=sys.stderr)


def ok(msg):
    print(f"\033[32mOK:\033[0m    {msg}")


def ask(prompt, default_no=True):
    suffix = "[y/N] " if default_no else "[Y/n] "
    resp = input(f"{prompt} {suffix}").strip().lower()
    if not resp:
        return not default_no
    return resp in ("y", "yes")


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)

    ino_path = sys.argv[1]
    bin_path = sys.argv[2]

    if not os.path.isfile(ino_path):
        err(f"{ino_path} not found")
        sys.exit(1)
    if not os.path.isfile(bin_path):
        err(f"{bin_path} not found")
        sys.exit(1)

    # 1. Extract FW_VERSION
    with open(ino_path, encoding="utf-8") as f:
        src = f.read()
    m = re.search(
        r'FW_VERSION\s*=\s*"([0-9]{4}\.[0-9]{2}\.[0-9]{2}\.[0-9]+)"',
        src,
    )
    if not m:
        err(f"Could not extract FW_VERSION from {ino_path}")
        err('Expected:  static const char* FW_VERSION = "YYYY.MM.DD.NNNN";')
        sys.exit(1)

    fw_version = m.group(1)
    build = fw_version.split(".")[-1]
    bin_name = f"Holly_RDiQ5000_{fw_version}.bin"
    release_tag = build

    print()
    print(f"Firmware version:  {fw_version}")
    print(f"Release tag:       {release_tag}")
    print(f"Binary filename:   {bin_name}")

    # 2. Sanity check against current manifest.json
    if os.path.isfile("manifest.json"):
        try:
            with open("manifest.json", encoding="utf-8") as f:
                m_data = json.load(f)
            cur_v = m_data.get("version", "")
            if cur_v:
                print(f"Current manifest:  {cur_v}")
                if cur_v == fw_version:
                    warn("New version equals the current manifest version.")
                    if not ask("Re-release anyway?"):
                        sys.exit(1)
        except Exception as e:
            warn(f"Could not parse current manifest.json: {e}")
    else:
        warn("manifest.json not found in cwd — will create a fresh one.")

    # 3. Copy / rename binary to the canonical filename
    bin_dir = os.path.dirname(bin_path) or "."
    release_bin = os.path.join(bin_dir, bin_name)
    if os.path.abspath(bin_path) != os.path.abspath(release_bin):
        shutil.copy(bin_path, release_bin)
        ok(f"Copied binary -> {release_bin}")
    else:
        ok(f"Binary already named correctly: {release_bin}")

    # 4. Check gh CLI
    if not shutil.which("gh"):
        err("gh CLI not found. Install from https://cli.github.com")
        err("then run `gh auth login` to authenticate to your GitHub account.")
        sys.exit(1)

    # 5. Existing release?
    chk = subprocess.run(
        ["gh", "release", "view", release_tag],
        capture_output=True,
    )
    if chk.returncode == 0:
        warn(f"Release '{release_tag}' already exists on GitHub.")
        if not ask("Delete and recreate?"):
            sys.exit(1)
        subprocess.run(
            ["gh", "release", "delete", release_tag, "-y"],
            check=True,
        )
        ok(f"Deleted existing release {release_tag}")

    # 6. Create release
    subprocess.run(
        [
            "gh",
            "release",
            "create",
            release_tag,
            release_bin,
            "--title",
            f"Holly firmware {fw_version}",
            "--notes",
            f"Compiled from {os.path.basename(ino_path)}.",
        ],
        check=True,
    )
    ok(f"Created GitHub release {release_tag}")

    # 7. Get repo nameWithOwner for URL construction
    res = subprocess.run(
        ["gh", "repo", "view", "--json", "nameWithOwner", "-q", ".nameWithOwner"],
        capture_output=True,
        text=True,
        check=True,
    )
    repo = res.stdout.strip()
    release_url = (
        f"https://github.com/{repo}/releases/download/{release_tag}/{bin_name}"
    )

    # 8. Write manifest.json
    manifest_data = {"version": fw_version, "url": release_url}
    with open("manifest.json", "w", encoding="utf-8") as f:
        json.dump(manifest_data, f, indent=2)
        f.write("\n")
    ok("Updated manifest.json:")
    with open("manifest.json", encoding="utf-8") as f:
        print(f.read())

    # 9. Verify the release URL is reachable
    print("Verifying release binary is downloadable...")
    try:
        req = urllib.request.Request(release_url, method="HEAD")
        with urllib.request.urlopen(req, timeout=15) as resp:
            if 200 <= resp.status < 300:
                ok(f"Release URL returns HTTP {resp.status}")
            else:
                warn(f"Release URL returns HTTP {resp.status}")
    except urllib.error.HTTPError as e:
        warn(f"{release_url} returned HTTP {e.code}")
    except Exception as e:
        warn(f"Could not reach {release_url}: {e}")

    # 10. Commit + push
    in_git = (
        subprocess.run(
            ["git", "rev-parse", "--is-inside-work-tree"],
            capture_output=True,
        ).returncode
        == 0
    )
    if in_git:
        print()
        if ask("Commit and push manifest.json?"):
            subprocess.run(["git", "add", "manifest.json"], check=True)
            subprocess.run(
                ["git", "commit", "-m", f"Release firmware {fw_version}"],
                check=True,
            )
            subprocess.run(["git", "push"], check=True)
            ok("Pushed manifest update.")
    else:
        warn("Not inside a git working tree — skipping commit/push.")

    print()
    ok(
        f"Done. Devices will pick up {fw_version} on their next OTA check "
        f"(~60 seconds for boot-window devices, up to 1 hour for steady-state)."
    )


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nAborted.", file=sys.stderr)
        sys.exit(130)
    except subprocess.CalledProcessError as e:
        err(f"Command failed: {' '.join(e.cmd)}")
        sys.exit(e.returncode)
