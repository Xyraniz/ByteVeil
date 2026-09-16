cmake_minimum_required(VERSION 3.20)
project(ByteVeil VERSION 0.4.8 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
add_compile_options(-include stdint.h)

add_executable(byteveil byteveil_cli.cpp byteveil_decompiler/Lua51.cpp byteveil_decompiler/Lua51.h byteveil_decompiler/Protectors.cpp byteveil_decompiler/Protectors.h byteveil_decompiler/Unpack.cpp byteveil_decompiler/Unpack.h byteveil_decompiler/IR.cpp byteveil_decompiler/AstGen/src/Common.cpp byteveil_decompiler/BlockGen/include/Lifter.h byteveil_decompiler/BlockGen/src/Lifter.cpp byteveil_decompiler/Common.h byteveil_decompiler/BlockGen/src/BlockGen.cpp byteveil_decompiler/BlockGen/include/BlockGen.h byteveil_decompiler/AstGen/include/VirtualAstStack.h byteveil_decompiler/AstGen/src/VirtualAstStack.cpp byteveil_decompiler/BlockGen/include/BodyHandler.h byteveil_decompiler/BlockGen/src/BodyHandler.cpp byteveil_decompiler/AstGen/include/Generator.h byteveil_decompiler/AstGen/src/Generator.cpp byteveil_decompiler/AstGen/include/Handlers.h byteveil_decompiler/AstGen/src/Handlers.cpp byteveil_decompiler/Decompile.h byteveil_decompiler/Decompile.cpp)

include(CTest)
enable_testing()

target_include_directories(byteveil PRIVATE include)

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
