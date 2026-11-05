# Pacotes nativos devem descrever a maquina que compila seus executores.
# A compilacao atual e nativa Linux; isto nao configura cross-compilation.
if(NOT DEFINED MANIFEST OR NOT EXISTS "${MANIFEST}")
    message(FATAL_ERROR "Manifesto de pacote nao encontrado")
endif()
execute_process(
    COMMAND getconf GNU_LIBC_VERSION OUTPUT_VARIABLE LIBC OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE LIBC_RESULT
)
execute_process(
    COMMAND uname -m OUTPUT_VARIABLE ARCH OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE ARCH_RESULT
)
if(NOT LIBC_RESULT EQUAL 0 OR NOT LIBC MATCHES "^glibc ([0-9]+\\.[0-9]+)$")
    message(FATAL_ERROR "Nao foi possivel identificar a glibc de compilacao")
endif()
set(GLIBC_VERSION "${CMAKE_MATCH_1}")
if(NOT ARCH_RESULT EQUAL 0 OR NOT ARCH MATCHES "^[A-Za-z0-9_]+$")
    message(FATAL_ERROR "Nao foi possivel identificar a arquitetura nativa")
endif()
file(READ "${MANIFEST}" CONTENT)
string(JSON CONTENT SET "${CONTENT}" min_glibc "\"${GLIBC_VERSION}\"")
string(JSON CONTENT SET "${CONTENT}" platforms "[\"linux-${ARCH}\"]")
file(WRITE "${MANIFEST}" "${CONTENT}\n")
