# PlatformIO компилирует "src" и "components/*" собственным сборщиком, минуя
# ninja/CMake — из-за этого штатный idf_component_register(EMBED_TXTFILES ...)
# не подхватывается (генерируемый .S уходит по неверному пути). Поэтому
# сертификат превращается в обычный C-заголовок прямо на этапе конфигурации
# CMake (file(WRITE) выполняется сразу, а не как ninja custom command) —
# получаем самый обычный .h-файл, который PlatformIO компилирует как любой другой.
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/ca.crt" CA_CERT_LINES)
set(CA_CERT_C_LITERAL "")
foreach(line IN LISTS CA_CERT_LINES)
    string(APPEND CA_CERT_C_LITERAL "\"${line}\\n\"\n")
endforeach()
file(WRITE "${CMAKE_CURRENT_LIST_DIR}/ca_cert_pem.h"
     "static const char CA_CERT_PEM[] =\n${CA_CERT_C_LITERAL};\n")

idf_component_register(INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}")
