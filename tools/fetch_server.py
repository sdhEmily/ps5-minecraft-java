#!/usr/bin/env python3
"""Download an official Minecraft Java server JAR and verify Mojang's SHA-1."""

import argparse
import hashlib
from http.client import IncompleteRead
import json
import os
from pathlib import Path
import tempfile
import urllib.request


MANIFEST_URL = "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json"
ROOT = Path(__file__).resolve().parents[1]


def read_json(url: str) -> dict:
    request = urllib.request.Request(url, headers={"User-Agent": "ps5-minecraft-java/0.1"})
    with urllib.request.urlopen(request, timeout=30) as response:
        return json.load(response)


def download(url: str, destination: Path, expected_sha1: str, expected_size: int) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    last_error = None
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=destination.parent, delete=False) as output:
            temporary = Path(output.name)
            while output.tell() < expected_size:
                offset = output.tell()
                headers = {"User-Agent": "ps5-minecraft-java/0.1"}
                if offset:
                    headers["Range"] = f"bytes={offset}-"
                request = urllib.request.Request(url, headers=headers)
                try:
                    with urllib.request.urlopen(request, timeout=60) as response:
                        if offset and response.status != 206:
                            raise IOError("Server did not honor a ranged resume request")
                        try:
                            while chunk := response.read(1024 * 1024):
                                output.write(chunk)
                        except IncompleteRead as incomplete:
                            output.write(incomplete.partial)
                except Exception as error:
                    last_error = error
                    if output.tell() == offset:
                        raise
                output.flush()
                if output.tell() <= offset:
                    raise IOError(f"Download stopped at {output.tell()} of {expected_size} bytes")
                if output.tell() > expected_size:
                    raise IOError(f"Download exceeded expected size {expected_size}")
        digest = hashlib.sha1(temporary.read_bytes()).hexdigest()
        if digest != expected_sha1:
            raise ValueError(f"SHA-1 mismatch: expected {expected_sha1}, got {digest}")
        os.replace(temporary, destination)
        return
    except Exception as error:
        last_error = error
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    raise RuntimeError(f"Could not download verified server JAR: {last_error}") from last_error


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("version", nargs="?", default="26.1.1", help="Minecraft Java version (default: 26.1.1)")
    parser.add_argument("--output", type=Path, default=ROOT / "build/server/server.jar")
    args = parser.parse_args()

    manifest = read_json(MANIFEST_URL)
    release = next((entry for entry in manifest["versions"] if entry["id"] == args.version), None)
    if release is None:
        parser.error(f"Version {args.version!r} is not in Mojang's version manifest")
    version = read_json(release["url"])
    artifact = version.get("downloads", {}).get("server")
    if not artifact:
        parser.error(f"Version {args.version!r} has no official server JAR")

    download(artifact["url"], args.output, artifact["sha1"], artifact["size"])
    metadata_path = args.output.with_suffix(".metadata.json")
    metadata_path.write_text(json.dumps({
        "version": args.version,
        "url": artifact["url"],
        "sha1": artifact["sha1"],
        "size": artifact.get("size"),
    }, indent=2) + "\n", encoding="utf-8")
    print(f"Verified Minecraft server {args.version}: {args.output}")
    print(f"SHA-1: {artifact['sha1']}")


if __name__ == "__main__":
    main()
