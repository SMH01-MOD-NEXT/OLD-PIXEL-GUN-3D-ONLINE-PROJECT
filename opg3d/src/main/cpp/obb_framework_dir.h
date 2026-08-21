#pragma once

// Prepare the app-owned OBB directory through Android's storage framework
// before the native OBB provisioner runs.
//
// Android 11+ intentionally rejects a raw mkdir/open under Android/obb even
// when the path names the calling package. Context.getObbDir() delegates
// directory creation to StorageManager/vold, which associates the directory
// with the app UID. The existing native provisioner can then keep doing the
// large, atomic extraction without Java byte arrays or an extra dex payload.
//
// This header is pulled in by config.h. Its priority-101 constructor runs
// before main.cpp's default-priority constructor, which starts the extraction.
// Several translation units include config.h; the inline atomics below make
// the bootstrap execute exactly once.

#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <dlfcn.h>
#include <jni.h>
#include <sys/stat.h>
#include <unistd.h>

#include "log.h"

namespace obb_framework_dir {

namespace detail {

constexpr size_t kPathCap = 640u;

inline std::atomic<bool> g_attempted{false};
inline std::atomic<bool> g_ready{false};

using GetCreatedJavaVMs = jint (*)(JavaVM**, jsize, jsize*);

inline bool clear_exception(JNIEnv* env) {
    if (env == nullptr || !env->ExceptionCheck()) return false;
    env->ExceptionClear();
    return true;
}

inline JavaVM* find_created_vm() {
    void* symbol = dlsym(RTLD_DEFAULT, "JNI_GetCreatedJavaVMs");

    // libart is already present in every app process, but some Android linker
    // namespaces do not expose all of its symbols through RTLD_DEFAULT. Try
    // the two platform libraries that have exported the invocation API across
    // Android releases. Failure is harmless: the old native path remains.
    if (symbol == nullptr) {
        const char* libraries[] = {"libart.so", "libnativehelper.so"};
        for (const char* library : libraries) {
            void* handle = dlopen(library, RTLD_NOW | RTLD_LOCAL);
            if (handle == nullptr) continue;
            symbol = dlsym(handle, "JNI_GetCreatedJavaVMs");
            if (symbol != nullptr) break;
        }
    }
    if (symbol == nullptr) return nullptr;

    JavaVM* vm = nullptr;
    jsize count = 0;
    const auto get_created_vms = reinterpret_cast<GetCreatedJavaVMs>(symbol);
    if (get_created_vms(&vm, 1, &count) != JNI_OK || count < 1 || vm == nullptr) {
        return nullptr;
    }
    return vm;
}

inline jobject call_application_getter(JNIEnv* env, const char* class_name,
                                       const char* method_name) {
    jclass type = env->FindClass(class_name);
    if (type == nullptr || clear_exception(env)) return nullptr;

    jmethodID getter = env->GetStaticMethodID(
        type, method_name, "()Landroid/app/Application;");
    if (getter == nullptr || clear_exception(env)) {
        env->DeleteLocalRef(type);
        return nullptr;
    }

    jobject application = env->CallStaticObjectMethod(type, getter);
    const bool failed = clear_exception(env);
    env->DeleteLocalRef(type);
    return failed ? nullptr : application;
}

inline jobject current_application(JNIEnv* env) {
    // ActivityThread is available before an Activity is created; by the time
    // Unity asks Java to load its native library, the Application is bound.
    jobject application = call_application_getter(
        env, "android/app/ActivityThread", "currentApplication");
    if (application != nullptr) return application;

    application = call_application_getter(
        env, "android/app/AppGlobals", "getInitialApplication");
    if (application != nullptr) return application;

    // Last fallback for platform builds where currentApplication is hidden
    // more aggressively but the ActivityThread instance remains reachable.
    jclass activity_thread = env->FindClass("android/app/ActivityThread");
    if (activity_thread == nullptr || clear_exception(env)) return nullptr;
    jmethodID current = env->GetStaticMethodID(
        activity_thread, "currentActivityThread", "()Landroid/app/ActivityThread;");
    if (current == nullptr || clear_exception(env)) {
        env->DeleteLocalRef(activity_thread);
        return nullptr;
    }
    jobject thread = env->CallStaticObjectMethod(activity_thread, current);
    if (thread == nullptr || clear_exception(env)) {
        env->DeleteLocalRef(activity_thread);
        return nullptr;
    }
    jmethodID get_application = env->GetMethodID(
        activity_thread, "getApplication", "()Landroid/app/Application;");
    if (get_application == nullptr || clear_exception(env)) {
        env->DeleteLocalRef(thread);
        env->DeleteLocalRef(activity_thread);
        return nullptr;
    }
    application = env->CallObjectMethod(thread, get_application);
    const bool failed = clear_exception(env);
    env->DeleteLocalRef(thread);
    env->DeleteLocalRef(activity_thread);
    return failed ? nullptr : application;
}

// ContextImpl normally reaches this path itself when getObbDir() notices a
// missing directory. Calling it explicitly is useful when a file manager made
// the directory first with the wrong FUSE owner: StorageManager forwards the
// request to vold using the calling package name. The method is hidden but has
// existed throughout the supported Android range; absence or hidden-API
// rejection simply leaves getObbDir() as the only framework attempt.
inline bool ask_storage_manager_to_prepare(JNIEnv* env, jobject context, jobject directory) {
    jclass context_type = env->FindClass("android/content/Context");
    if (context_type == nullptr || clear_exception(env)) return false;
    jmethodID get_service = env->GetMethodID(
        context_type, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
    if (get_service == nullptr || clear_exception(env)) {
        env->DeleteLocalRef(context_type);
        return false;
    }
    jstring storage_name = env->NewStringUTF("storage");
    if (storage_name == nullptr || clear_exception(env)) {
        env->DeleteLocalRef(context_type);
        return false;
    }
    jobject manager = env->CallObjectMethod(context, get_service, storage_name);
    if (manager == nullptr || clear_exception(env)) {
        env->DeleteLocalRef(storage_name);
        env->DeleteLocalRef(context_type);
        return false;
    }

    jclass storage_type = env->FindClass("android/os/storage/StorageManager");
    if (storage_type == nullptr || clear_exception(env)) {
        env->DeleteLocalRef(manager);
        env->DeleteLocalRef(storage_name);
        env->DeleteLocalRef(context_type);
        return false;
    }
    jmethodID mkdirs = env->GetMethodID(
        storage_type, "mkdirs", "(Ljava/io/File;)V");
    if (mkdirs == nullptr || clear_exception(env)) {
        env->DeleteLocalRef(storage_type);
        env->DeleteLocalRef(manager);
        env->DeleteLocalRef(storage_name);
        env->DeleteLocalRef(context_type);
        return false;
    }

    env->CallVoidMethod(manager, mkdirs, directory);
    const bool ok = !clear_exception(env);
    env->DeleteLocalRef(storage_type);
    env->DeleteLocalRef(manager);
    env->DeleteLocalRef(storage_name);
    env->DeleteLocalRef(context_type);
    return ok;
}

inline bool prepare_with_vm(JavaVM* vm) {
    if (vm == nullptr) return false;

    JNIEnv* env = nullptr;
    bool attached_here = false;
    const jint state = vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (state == JNI_EDETACHED) {
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK || env == nullptr) {
            return false;
        }
        attached_here = true;
    } else if (state != JNI_OK || env == nullptr) {
        return false;
    }

    if (env->PushLocalFrame(24) != JNI_OK) {
        if (attached_here) vm->DetachCurrentThread();
        return false;
    }

    bool ok = false;
    do {
        jobject application = current_application(env);
        if (application == nullptr) {
            LOGW("obb: Java VM is live but the Application context is not available during "
                 "native loading; trying legacy native storage paths");
            break;
        }

        jclass context_type = env->FindClass("android/content/Context");
        if (context_type == nullptr || clear_exception(env)) break;
        jmethodID get_obb_dir = env->GetMethodID(
            context_type, "getObbDir", "()Ljava/io/File;");
        if (get_obb_dir == nullptr || clear_exception(env)) break;

        jobject directory = env->CallObjectMethod(application, get_obb_dir);
        if (directory == nullptr || clear_exception(env)) {
            LOGW("obb: Context.getObbDir() returned no usable directory; trying legacy "
                 "native storage paths");
            break;
        }

        // Ensure the request goes through StorageManager/vold even if an
        // externally created directory already exists with unusable ownership.
        const bool storage_manager_ok =
            ask_storage_manager_to_prepare(env, application, directory);

        jclass file_type = env->FindClass("java/io/File");
        if (file_type == nullptr || clear_exception(env)) break;
        jmethodID get_absolute_path = env->GetMethodID(
            file_type, "getAbsolutePath", "()Ljava/lang/String;");
        if (get_absolute_path == nullptr || clear_exception(env)) break;
        auto path_string = static_cast<jstring>(
            env->CallObjectMethod(directory, get_absolute_path));
        if (path_string == nullptr || clear_exception(env)) break;

        const char* path_chars = env->GetStringUTFChars(path_string, nullptr);
        if (path_chars == nullptr || clear_exception(env)) break;
        char obb_directory[kPathCap];
        std::snprintf(obb_directory, sizeof(obb_directory), "%s", path_chars);
        env->ReleaseStringUTFChars(path_string, path_chars);

        constexpr const char* marker = "/Android/obb/";
        const char* split = std::strstr(obb_directory, marker);
        if (split == nullptr || split == obb_directory) {
            LOGW("obb: Context.getObbDir() returned unexpected path '%s'; trying legacy "
                 "native storage paths", obb_directory);
            break;
        }
        const size_t root_length = static_cast<size_t>(split - obb_directory);
        if (root_length >= kPathCap) break;
        char external_root[kPathCap];
        std::memcpy(external_root, obb_directory, root_length);
        external_root[root_length] = '\0';

        struct stat info {};
        if (stat(obb_directory, &info) != 0 || !S_ISDIR(info.st_mode)) {
            LOGW("obb: Android framework did not materialize '%s': %s; trying legacy "
                 "native storage paths", obb_directory, std::strerror(errno));
            break;
        }
        if (setenv("EXTERNAL_STORAGE", external_root, 1) != 0) {
            LOGW("obb: cannot select framework external root '%s': %s", external_root,
                 std::strerror(errno));
            break;
        }

        if (access(obb_directory, W_OK) != 0) {
            LOGW("obb: framework OBB directory '%s' still reports non-writable (%s); "
                 "StorageManager.mkdirs=%s, the extraction open() will verify it",
                 obb_directory, std::strerror(errno),
                 storage_manager_ok ? "accepted" : "unavailable");
        } else {
            LOGI("obb: Android framework prepared '%s' for this app UID; native extraction "
                 "will use external root '%s'", obb_directory, external_root);
        }
        ok = true;
    } while (false);

    env->PopLocalFrame(nullptr);
    if (attached_here) vm->DetachCurrentThread();
    return ok;
}

} // namespace detail

inline bool prepare() {
    if (detail::g_ready.load()) return true;
    bool expected = false;
    if (!detail::g_attempted.compare_exchange_strong(expected, true)) {
        return detail::g_ready.load();
    }

    JavaVM* vm = detail::find_created_vm();
    if (vm == nullptr) {
        LOGW("obb: JNI invocation API is not visible during native loading; trying legacy "
             "native storage paths");
        return false;
    }
    const bool ready = detail::prepare_with_vm(vm);
    detail::g_ready.store(ready);
    return ready;
}

} // namespace obb_framework_dir

// Android's VM and Application already exist when Unity asks Java to load its
// native library. Run before main.cpp's default-priority constructor so
// Context.getObbDir()/StorageManager can prepare the FUSE-owned package path
// before obb_provisioner::provision() calls open().
__attribute__((constructor(101))) static void opg3d_prepare_framework_obb_directory() {
    (void)::obb_framework_dir::prepare();
}
