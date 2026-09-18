cmake_minimum_required(VERSION 3.20)
project(ByteVeil VERSION 0.4.8 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
add_compile_options(-include stdint.h)

add_executable(byteveil byteveil_cli.cpp byteveil_decompiler/Lua51.cpp byteveil_decompiler/Lua51.h byteveil_decompiler/Protectors.cpp byteveil_decompiler/Protectors.h byteveil_decompiler/Unpack.cpp byteveil_decompiler/Unpack.h byteveil_decompiler/IR.cpp byteveil_decompiler/AstGen/src/Common.cpp byteveil_decompiler/BlockGen/include/Lifter.h byteveil_decompiler/BlockGen/src/Lifter.cpp byteveil_decompiler/Common.h byteveil_decompiler/BlockGen/src/BlockGen.cpp byteveil_decompiler/BlockGen/include/BlockGen.h byteveil_decompiler/AstGen/include/VirtualAstStack.h byteveil_decompiler/AstGen/src/VirtualAstStack.cpp byteveil_decompiler/BlockGen/include/BodyHandler.h byteveil_decompiler/BlockGen/src/BodyHandler.cpp byteveil_decompiler/AstGen/include/Generator.h byteveil_decompiler/AstGen/src/Generator.cpp byteveil_decompiler/AstGen/include/Handlers.h byteveil_decompiler/AstGen/src/Handlers.cpp byteveil_decompiler/Decompile.h byteveil_decompiler/Decompile.cpp byteveil_decompiler/Restructure.cpp byteveil_decompiler/Restructure.h)

include(CTest)
enable_testing()
set(BYTEVEIL_TEST_SCRIPTS
    test_cli.sh
    test_lua51.sh
    test_lua51_extended.sh
    test_lua51_reconstruction.sh
    test_luau_decompile.sh
    test_luau_regressions.sh)
foreach(TEST_SCRIPT IN LISTS BYTEVEIL_TEST_SCRIPTS)
    add_test(NAME byteveil_${TEST_SCRIPT} COMMAND bash ${CMAKE_SOURCE_DIR}/tests/${TEST_SCRIPT})
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

add_subdirectory(luau)
target_link_libraries(byteveil Luau.Compiler)
target_link_libraries(byteveil Luau.Analysis)
target_link_libraries(byteveil Luau.VM)
target_link_libraries(byteveil Luau.Ast)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
