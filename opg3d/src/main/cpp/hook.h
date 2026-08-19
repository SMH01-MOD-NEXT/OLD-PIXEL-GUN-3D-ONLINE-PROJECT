#pragma once

namespace hook {

struct ManagedMethod {
    const char* namespaze;
    const char* klass;
    const char* method;
    int args_count;
};

// Находит MethodInfo через IL2CPP metadata и ставит shadowhook inline-hook на
// его реальный methodPointer. Поэтому в коде нет абсолютных RVA и он
// fail-closed: если класс/метод не соответствует ожидаемой сборке, адрес не
// патчится.
bool install(const ManagedMethod& target, void* replacement, void** original,
             bool required = false);

// Человекочитаемая версия используемого inline-hook движка (для логов).
const char* engine_version();

} // namespace hook
