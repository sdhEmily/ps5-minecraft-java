#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
java_repo=${PS5_JAVA25_HOME:-"$(cd -- "$root/../ps5-java25-homebrew" 2>/dev/null && pwd || true)"}
runtime="$java_repo/Development"
if [[ -z "$java_repo" || ! -f "$runtime/tools/package_native.py" ]]; then
    echo 'Set PS5_JAVA25_HOME to a checkout of mateo-cogeanu/ps5-java25-homebrew.' >&2
    exit 2
fi

# Reuse the Java project's pinned embedded HotSpot and boot-image build. Its
# packaging helper links this project's game-specific JNI entry point.
if ! compgen -G "$runtime/.tools/java25/openjdk/build/ps5-bootstrap/hotspot/variant-server/libjvm/objs/*.o" >/dev/null \
        || [[ ! -f "$runtime/.tools/java25/openjdk/build/ps5-bootstrap/support/modules_libs/java.base/server/libjvm.so" ]]; then
    JAVA25_BOOT_JDK=${JAVA25_BOOT_JDK:-/opt/homebrew/opt/openjdk@25/libexec/openjdk.jdk/Contents/Home} \
        LLVM_CONFIG=${LLVM_CONFIG:-/opt/homebrew/opt/llvm/bin/llvm-config} \
        python3 "$runtime/tools/java25_source.py" --build-hotspot --build-base
fi
PS5_PAYLOAD_SDK="$runtime/.tools/native-sdk/ps5-payload-sdk" make -C "$runtime" java25-startup
bash "$root/tools/build_server_bootstrap.sh"

assets="$root/build/server/ps5-assets"
python3 - "$runtime/build/java25/startup-assets" "$assets" <<'PY'
import shutil
import sys
from pathlib import Path
source, destination = map(Path, sys.argv[1:])
if not source.is_dir():
    raise SystemExit(f"Missing built Java runtime assets: {source}")
if destination.exists():
    shutil.rmtree(destination)
shutil.copytree(source, destination)
PY
cp -R "$root/build/server/bootstrap/classes/ps5" "$assets/java25/classes/"
rm -f "$assets/java25/classes/Hello25.class"
rm -rf "$assets/sounds"

python3 "$runtime/tools/package_native.py" \
    --source "$root/native/minecraft_server.cpp" \
    --param "$root/sce_sys/param.json" \
    --icon "$root/sce_sys/icon0.png" \
    --extra-link-response "$runtime/build/java25/jni-link.rsp" \
    --linker-script "$runtime/build/java25/jni-probe.ld" \
    --asset-dir "$assets" \
    --license-file "$runtime/.tools/java25/openjdk/LICENSE" \
    --license-file "$runtime/.tools/java25/openjdk/ADDITIONAL_LICENSE_INFO" \
    --license-file "$runtime/.tools/java25/openjdk/ASSEMBLY_EXCEPTION" \
    --license-file "$runtime/java25/EMBEDDED-RUNTIME-NOTICES.md"

mkdir -p "$root/dist"
python3 - "$runtime/dist/PPSA99873" "$root/dist/PPSA99873" <<'PY'
import zipfile
import shutil
import sys
from pathlib import Path
source, destination = map(Path, sys.argv[1:])
if destination.exists():
    shutil.rmtree(destination)
shutil.copytree(source, destination)
archive = destination.parent / "PPSA99873.zip"
archive.unlink(missing_ok=True)
with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as bundle:
    for path in sorted(destination.rglob("*")):
        if path.is_file():
            bundle.write(path, path.relative_to(destination.parent))
print(f"PS5 server title package: {destination}")
print(f"PS5 server title archive: {archive}")
PY
