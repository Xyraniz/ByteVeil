cmake_minimum_required(VERSION 3.20)
project(ByteVeil VERSION 0.4.8 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)

# Luau's vendored build still declares a 3.0 policy baseline. CMake 4 removed
# that compatibility mode, but allows a parent project to provide a newer
# policy floor without modifying the vendored sources.
if(CMAKE_VERSION VERSION_GREATER_EQUAL 4.0)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.10)
endif()

if(MSVC)
    add_compile_options(/FIstdint.h)
else()
    add_compile_options(-include stdint.h)
endif()

add_executable(byteveil byteveil_cli.cpp byteveil_decompiler/Lua51.cpp byteveil_decompiler/Lua51.h byteveil_decompiler/Protectors.cpp byteveil_decompiler/Protectors.h byteveil_decompiler/Unpack.cpp byteveil_decompiler/Unpack.h byteveil_decompiler/IR.cpp byteveil_decompiler/AstGen/src/Common.cpp byteveil_decompiler/BlockGen/include/Lifter.h byteveil_decompiler/BlockGen/src/Lifter.cpp byteveil_decompiler/Common.h byteveil_decompiler/BlockGen/src/BlockGen.cpp byteveil_decompiler/BlockGen/include/BlockGen.h byteveil_decompiler/AstGen/include/VirtualAstStack.h byteveil_decompiler/AstGen/src/VirtualAstStack.cpp byteveil_decompiler/BlockGen/include/BodyHandler.h byteveil_decompiler/BlockGen/src/BodyHandler.cpp byteveil_decompiler/AstGen/include/Generator.h byteveil_decompiler/AstGen/src/Generator.cpp byteveil_decompiler/AstGen/include/Handlers.h byteveil_decompiler/AstGen/src/Handlers.cpp byteveil_decompiler/Decompile.h byteveil_decompiler/Decompile.cpp byteveil_decompiler/Restructure.cpp byteveil_decompiler/Restructure.h luau/Analysis/src/Transpiler.cpp)

include(CTest)
enable_testing()
set(BYTEVEIL_TEST_SCRIPTS
    test_cli.sh
    test_lua51.sh
    test_lua51_extended.sh
    test_lua51_reconstruction.sh
    test_luau_decompile.sh
    test_luau_regressions.sh
    test_ir_semantics.sh)
foreach(TEST_SCRIPT IN LISTS BYTEVEIL_TEST_SCRIPTS)
    add_test(NAME byteveil_${TEST_SCRIPT} COMMAND bash ${CMAKE_SOURCE_DIR}/tests/${TEST_SCRIPT} $<TARGET_FILE:byteveil>)
    set_tests_properties(byteveil_${TEST_SCRIPT} PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endforeach()

target_include_directories(byteveil PRIVATE
    include
    ${CMAKE_SOURCE_DIR}/luau/Ast/include
    ${CMAKE_SOURCE_DIR}/luau/Common/include
    ${CMAKE_SOURCE_DIR}/luau/Compiler/include
    ${CMAKE_SOURCE_DIR}/luau/VM/include
    ${CMAKE_SOURCE_DIR}/luau/Analysis/include)

option(LUAU_BUILD_CLI "Build CLI" OFF)
option(LUAU_BUILD_TESTS "Build tests" OFF)
option(LUAU_BUILD_WEB "Build Web module" OFF)
option(LUAU_WERROR "Warnings as errors" OFF)
option(LUAU_STATIC_CRT "Link with the static CRT (/MT)" OFF)

add_subdirectory(luau EXCLUDE_FROM_ALL)
target_link_libraries(byteveil PRIVATE Luau.Compiler Luau.VM Luau.Ast)
if(MINGW)
    # Git Bash and other MSYS environments often prepend their own, possibly
    # ABI-incompatible libstdc++ DLL to PATH. Keep the produced CLI standalone.
    target_link_options(byteveil PRIVATE -static-libgcc -static-libstdc++)
endif()

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
