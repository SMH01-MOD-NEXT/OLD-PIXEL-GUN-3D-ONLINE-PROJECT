#pragma once

namespace hook {

struct ManagedMethod {
    const char* namespaze;
    const char* klass;
    const char* method;
    int args_count;
};

// Finds the MethodInfo through IL2CPP metadata and installs a shadowhook
// inline hook on its real methodPointer. There are no absolute RVAs anywhere
// in the code and the wrapper is fail-closed: if the class/method does not
// match the expected build, no address is patched.
bool install(const ManagedMethod& target, void* replacement, void** original,
             bool required = false);

// Human-readable version of the inline-hook engine (for logs).
const char* engine_version();

} // namespace hook
