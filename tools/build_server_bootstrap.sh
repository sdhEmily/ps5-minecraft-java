#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
out="$root/build/server/bootstrap"
jdk=${JAVA_HOME:-}
if [[ -z "$jdk" && -x /opt/homebrew/opt/openjdk@25/libexec/openjdk.jdk/Contents/Home/bin/javac ]]; then
    jdk=/opt/homebrew/opt/openjdk@25/libexec/openjdk.jdk/Contents/Home
fi
javac=${JAVAC:-${jdk:+$jdk/bin/}javac}
jar=${JAR:-${jdk:+$jdk/bin/}jar}
mkdir -p "$out/classes"
"$javac" --release 25 -encoding UTF-8 -d "$out/classes" \
    "$root/Development/server/MinecraftServerBootstrap.java"
"$jar" --create --file "$out/minecraft-server-bootstrap.jar" \
    --main-class ps5.minecraft.server.MinecraftServerBootstrap -C "$out/classes" .
echo "$out/minecraft-server-bootstrap.jar"
