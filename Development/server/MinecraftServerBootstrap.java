/* SPDX-License-Identifier: MIT */
package ps5.minecraft.server;

import java.io.IOException;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.net.URL;
import java.net.URLClassLoader;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.jar.Attributes;
import java.util.jar.JarFile;
import java.util.jar.Manifest;

/** Launches Mojang's bundled server from the already-running PS5 JVM. */
public final class MinecraftServerBootstrap {
    private MinecraftServerBootstrap() {}

    public static void main(String[] args) throws Exception {
        if (Runtime.version().feature() != 25) {
            throw new IllegalStateException("Minecraft 26.1.1 requires Java 25; found "
                    + Runtime.version());
        }
        if (args.length < 1) {
            throw new IllegalArgumentException("Usage: MinecraftServerBootstrap <server.jar> [server args...]");
        }

        Path serverJar = Path.of(args[0]).toAbsolutePath().normalize();
        Path workingDirectory = Path.of("").toAbsolutePath().normalize();
        requireEulaAccepted(workingDirectory.resolve("eula.txt"));
        if (!Files.isRegularFile(serverJar)) {
            throw new IOException("Server JAR not found: " + serverJar);
        }

        String mainClass;
        try (JarFile jar = new JarFile(serverJar.toFile(), true)) {
            Manifest manifest = jar.getManifest();
            mainClass = manifest == null ? null
                    : manifest.getMainAttributes().getValue(Attributes.Name.MAIN_CLASS);
        }
        if (mainClass == null || mainClass.isBlank()) {
            throw new IOException("Server JAR has no Main-Class manifest entry: " + serverJar);
        }

        // `java -jar` exposes the launched archive through this property. The
        // Minecraft bundler uses it when finding its embedded server payload.
        System.setProperty("java.class.path", serverJar.toString());
        URL serverUrl = serverJar.toUri().toURL();
        try (URLClassLoader serverLoader = new URLClassLoader(
                new URL[]{serverUrl}, ClassLoader.getSystemClassLoader())) {
            Thread.currentThread().setContextClassLoader(serverLoader);
            Class<?> entry = Class.forName(mainClass, true, serverLoader);
            Method main = entry.getMethod("main", String[].class);
            String[] serverArgs = args.length == 1 ? new String[]{"nogui"}
                    : Arrays.copyOfRange(args, 1, args.length);
            try {
                main.invoke(null, (Object) serverArgs);
            } catch (InvocationTargetException error) {
                Throwable cause = error.getCause();
                if (cause instanceof Exception exception) throw exception;
                if (cause instanceof Error fatal) throw fatal;
                throw new RuntimeException(cause);
            }
        }
    }

    private static void requireEulaAccepted(Path eulaFile) throws IOException {
        if (!Files.isRegularFile(eulaFile)) {
            throw new IOException("Server EULA has not been accepted. Review the Minecraft EULA, "
                    + "then create " + eulaFile + " with the line eula=true.");
        }
        boolean accepted = Files.readAllLines(eulaFile, StandardCharsets.UTF_8).stream()
                .map(String::trim)
                .anyMatch(line -> line.equalsIgnoreCase("eula=true"));
        if (!accepted) {
            throw new IOException("Server EULA is not accepted. Review the Minecraft EULA, "
                    + "then set eula=true in " + eulaFile + ".");
        }
    }
}
