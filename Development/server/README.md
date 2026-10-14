# First server bootstrap

The first game-specific target is Mojang's official **Minecraft Java 26.1.1
dedicated server**. It requires Java 25, matching the current PS5 OpenJDK
runtime target. The server JAR remains a user-downloaded artifact and is not
committed or bundled by this project.

`MinecraftServerBootstrap.java` is the Java-side entry point for an embedded
PS5 JVM. It reads the server JAR's manifest, creates a URL class loader, and
invokes Mojang's bundled launcher in the existing VM. It does not spawn a
second Java process. The initial version also requires `eula=true` in the
server's writable working directory; it never accepts the EULA automatically.
`native/minecraft_server.cpp` embeds the existing PS5 JVM, defines the
bootstrap class from the app assets, sets the Java working directory to
`/data/minecraft` (without calling the sandbox-denied native `chdir`), and
records server output under `/data/minecraft/server.log`.

The server artifact can be fetched and checksum-verified from Mojang's version
manifest with:

```sh
python3 tools/fetch_server.py 26.1.1
```

The official 26.1.1 artifact currently has SHA-1
`49c8195703ad0ba4f0a4efbccfd85a4a8ca57431`. The download tool resolves the
version metadata each time and verifies the digest before replacing its output.

## Current status

- The Java bootstrap and PS5 JNI entry compile into an unsigned PS5 title
  package with the embedded Java 25 runtime.
- No Minecraft code has been run on PS5 yet.
- Console targets remain: reach the server `Done` state, connect from another
  device, save/reload a world, and verify clean shutdown.
- The current PS5 JVM uses interpreter mode, so server startup speed and tick
  performance are open questions.

## Build the PS5 title

Check out [ps5-java25-homebrew](https://github.com/mateo-cogeanu/ps5-java25-homebrew)
beside this repository, or set `PS5_JAVA25_HOME` to its checkout. Configure
that repository's documented macOS/PS5 build prerequisites, then run:

```sh
python3 tools/fetch_server.py 26.1.1
bash tools/build_ps5_server.sh
```

The build places the unsigned homebrew title folder at `dist/PPSA99873`. It
does not include Mojang's server JAR. Copy the official JAR to
`/data/minecraft/server.jar` on the console. Review the EULA yourself and
write `eula=true` to `/data/minecraft/eula.txt` only if you agree. The
server log is written to `/data/minecraft/server.log`.
