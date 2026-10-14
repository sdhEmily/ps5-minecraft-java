/* SPDX-License-Identifier: MIT */
#include <jni.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

// The embedded HotSpot link uses --wrap for these libc calls so the runtime
// probe can instrument them. Keep the same transparent forwarding symbols in
// this app-specific entry point.
extern "C" {
void *__real_memcpy(void *, const void *, size_t);
void *__real_memmove(void *, const void *, size_t);
int __real_memcmp(const void *, const void *, size_t);
int __real_strcmp(const char *, const char *);
size_t __real_strlen(const char *);
void *__real_memset(void *, int, size_t);
int __real_strncmp(const char *, const char *, size_t);
char *__real_strncpy(char *, const char *, size_t);
char *__real_strchr(const char *, int);
void *__real_memchr(const void *, int, size_t);
char *__real_strcpy(char *, const char *);
void *__real_malloc(size_t);
char *__real_strdup(const char *);

__attribute__((noinline)) void *__wrap_memcpy(void *dst, const void *src, size_t n) { return __real_memcpy(dst, src, n); }
__attribute__((noinline)) void *__wrap_memmove(void *dst, const void *src, size_t n) { return __real_memmove(dst, src, n); }
__attribute__((noinline)) int __wrap_memcmp(const void *a, const void *b, size_t n) { return __real_memcmp(a, b, n); }
__attribute__((noinline)) int __wrap_strcmp(const char *a, const char *b) { return __real_strcmp(a, b); }
__attribute__((noinline)) size_t __wrap_strlen(const char *s) { return __real_strlen(s); }
__attribute__((noinline)) void *__wrap_memset(void *dst, int value, size_t n) { return __real_memset(dst, value, n); }
__attribute__((noinline)) int __wrap_strncmp(const char *a, const char *b, size_t n) { return __real_strncmp(a, b, n); }
__attribute__((noinline)) char *__wrap_strncpy(char *dst, const char *src, size_t n) { return __real_strncpy(dst, src, n); }
__attribute__((noinline)) char *__wrap_strchr(const char *s, int value) { return __real_strchr(s, value); }
__attribute__((noinline)) void *__wrap_memchr(const void *s, int value, size_t n) { return __real_memchr(s, value, n); }
__attribute__((noinline)) char *__wrap_strcpy(char *dst, const char *src) { return __real_strcpy(dst, src); }
__attribute__((noinline)) void *__wrap_malloc(size_t n) { return __real_malloc(n); }
__attribute__((noinline)) char *__wrap_strdup(const char *s) { return __real_strdup(s); }
}

namespace {
constexpr const char *kRoot = "/data/minecraft";
constexpr const char *kServerJar = "/data/minecraft/server.jar";
constexpr const char *kBootstrapClass =
    "/app0/assets/java25/classes/ps5/minecraft/server/MinecraftServerBootstrap.class";
constexpr const char *kBootstrapName = "ps5/minecraft/server/MinecraftServerBootstrap";

void log_line(const char *message) {
    std::fprintf(stderr, "PS5_MINECRAFT_SERVER %s\n", message);
    std::fflush(stderr);
}

bool make_directory(const char *path) {
    if (mkdir(path, 0777) != 0 && errno != EEXIST) {
        std::fprintf(stderr, "PS5_MINECRAFT_SERVER mkdir %s failed errno=%d\n", path, errno);
        return false;
    }
    return true;
}

void make_directory_accessible(const char *path) {
    // This directory is also used to transfer the server JAR and read logs
    // through the user's FTP/file manager account. Correct permissions even
    // when it was created by an earlier build with mode 0700.
    if (chmod(path, 0777) != 0) {
        std::fprintf(stderr, "PS5_MINECRAFT_SERVER chmod %s failed errno=%d\n", path, errno);
    }
}

bool read_bootstrap(jbyte **bytes, jsize *length) {
    FILE *file = std::fopen(kBootstrapClass, "rb");
    if (!file) {
        std::fprintf(stderr, "PS5_MINECRAFT_SERVER cannot open bootstrap class errno=%d\n", errno);
        return false;
    }
    if (std::fseek(file, 0, SEEK_END) != 0) {
        std::fclose(file);
        return false;
    }
    long size = std::ftell(file);
    if (size < 8 || size > 1024 * 1024 || std::fseek(file, 0, SEEK_SET) != 0) {
        std::fclose(file);
        return false;
    }
    jbyte *data = new jbyte[static_cast<size_t>(size)];
    size_t count = std::fread(data, 1, static_cast<size_t>(size), file);
    bool failed = count != static_cast<size_t>(size) || std::ferror(file);
    std::fclose(file);
    if (failed || static_cast<unsigned char>(data[0]) != 0xca ||
        static_cast<unsigned char>(data[1]) != 0xfe ||
        static_cast<unsigned char>(data[2]) != 0xba ||
        static_cast<unsigned char>(data[3]) != 0xbe ||
        static_cast<unsigned char>(data[6]) != 0 ||
        static_cast<unsigned char>(data[7]) != 69) {
        delete[] data;
        log_line("bootstrap class is not valid Java 25 bytecode");
        return false;
    }
    *bytes = data;
    *length = static_cast<jsize>(size);
    return true;
}

void describe_exception(JNIEnv *env, const char *stage) {
    if (env->ExceptionCheck()) {
        std::fprintf(stderr, "PS5_MINECRAFT_SERVER Java exception during %s\n", stage);
        env->ExceptionDescribe();
        env->ExceptionClear();
    }
}
} // namespace

int main() {
    if (!make_directory(kRoot)) return 0;

    FILE *log = std::freopen("/data/minecraft/server.log", "a", stdout);
    if (log) std::freopen("/data/minecraft/server.log", "a", stderr);
    log_line("starting Java 25 VM");
    make_directory_accessible(kRoot);

    if (!make_directory("/data/minecraft/tmp")) return 0;
    make_directory_accessible("/data/minecraft/tmp");

    // On PS5, access() can report a sandbox denial even when opening the file
    // for reading is allowed. Check the operation the JVM will actually need.
    errno = 0;
    FILE *server_jar = std::fopen(kServerJar, "rb");
    if (!server_jar) {
        std::fprintf(stderr,
                     "PS5_MINECRAFT_SERVER cannot open %s for reading errno=%d\n",
                     kServerJar, errno);
        return 0;
    }
    std::fclose(server_jar);

    JavaVMOption options[] = {
        {const_cast<char *>("-Xint"), nullptr},
        {const_cast<char *>("-XX:UseAVX=0"), nullptr},
        {const_cast<char *>("-XX:UseSSE=2"), nullptr},
        {const_cast<char *>("-Xms128m"), nullptr},
        {const_cast<char *>("-Xmx2048m"), nullptr},
        {const_cast<char *>("-XX:CompressedClassSpaceSize=128m"), nullptr},
        {const_cast<char *>("-Xshare:off"), nullptr},
        {const_cast<char *>("-XX:+UseSerialGC"), nullptr},
        {const_cast<char *>("-XX:ActiveProcessorCount=4"), nullptr},
        {const_cast<char *>("-XX:-UsePerfData"), nullptr},
        {const_cast<char *>("-XX:ErrorFile=/data/minecraft/hs_err_java25_%p.log"), nullptr},
        {const_cast<char *>("-Djava.class.path=/app0/assets/java25/classes"), nullptr},
        {const_cast<char *>("-Djava.io.tmpdir=/data/minecraft/tmp"), nullptr},
        {const_cast<char *>("-Duser.home=/data/minecraft"), nullptr},
        {const_cast<char *>("-Duser.dir=/data/minecraft"), nullptr},
        {const_cast<char *>("-Dsun.nio.fs.chdirAllowed=true"), nullptr},
        {const_cast<char *>("-Duser.timezone=UTC"), nullptr},
    };
    JavaVMInitArgs args{};
    args.version = JNI_VERSION_24;
    args.nOptions = static_cast<jint>(sizeof(options) / sizeof(options[0]));
    args.options = options;
    args.ignoreUnrecognized = JNI_FALSE;

    JavaVM *vm = nullptr;
    JNIEnv *env = nullptr;
    jint status = JNI_CreateJavaVM(&vm, reinterpret_cast<void **>(&env), &args);
    if (status != JNI_OK || !vm || !env) {
        std::fprintf(stderr, "PS5_MINECRAFT_SERVER JNI_CreateJavaVM failed rc=%d\n", status);
        return 3;
    }
    log_line("Java VM ready; defining server bootstrap");

    jclass class_loader = env->FindClass("java/lang/ClassLoader");
    jmethodID get_system_loader = class_loader ? env->GetStaticMethodID(
        class_loader, "getSystemClassLoader", "()Ljava/lang/ClassLoader;") : nullptr;
    jobject loader = get_system_loader ? env->CallStaticObjectMethod(class_loader, get_system_loader) : nullptr;
    describe_exception(env, "system class loader lookup");

    jbyte *class_bytes = nullptr;
    jsize class_length = 0;
    jclass bootstrap = nullptr;
    if (loader && read_bootstrap(&class_bytes, &class_length)) {
        bootstrap = env->DefineClass(kBootstrapName, loader, class_bytes, class_length);
        delete[] class_bytes;
    }
    describe_exception(env, "bootstrap class definition");
    if (loader) env->DeleteLocalRef(loader);
    if (class_loader) env->DeleteLocalRef(class_loader);
    if (!bootstrap) {
        log_line("could not define MinecraftServerBootstrap");
        vm->DestroyJavaVM();
        return 4;
    }

    jmethodID main_method = env->GetStaticMethodID(bootstrap, "main", "([Ljava/lang/String;)V");
    jclass string_class = env->FindClass("java/lang/String");
    jobjectArray launch_args = string_class ? env->NewObjectArray(2, string_class, nullptr) : nullptr;
    jstring jar_path = env->NewStringUTF(kServerJar);
    jstring nogui = env->NewStringUTF("nogui");
    if (launch_args && jar_path && nogui) {
        env->SetObjectArrayElement(launch_args, 0, jar_path);
        env->SetObjectArrayElement(launch_args, 1, nogui);
    }
    describe_exception(env, "server arguments setup");
    if (!main_method || !launch_args) {
        log_line("could not prepare MinecraftServerBootstrap.main");
        vm->DestroyJavaVM();
        return 5;
    }

    log_line("calling MinecraftServerBootstrap; review EULA before setting eula=true");
    env->CallStaticVoidMethod(bootstrap, main_method, launch_args);
    describe_exception(env, "Minecraft server launch");
    log_line("MinecraftServerBootstrap returned");
    vm->DestroyJavaVM();
    return 0;
}
