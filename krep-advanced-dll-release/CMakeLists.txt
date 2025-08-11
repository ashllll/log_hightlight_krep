cmake_minimum_required(VERSION 3.10)
project(krep_dll_examples)

set(CMAKE_C_STANDARD 11)

# Include directories
include_directories(headers)

# Find libraries
find_library(KREP_ADVANCED_LIB 
    NAMES krep_advanced libkrep_advanced
    HINTS ${CMAKE_CURRENT_SOURCE_DIR}/advanced
)

find_library(KREP_SIMPLE_LIB 
    NAMES krep_simple libkrep_simple  
    HINTS ${CMAKE_CURRENT_SOURCE_DIR}/simple
)

# Advanced example
if(KREP_ADVANCED_LIB)
    add_executable(test_advanced examples/test_advanced.c)
    target_link_libraries(test_advanced ${KREP_ADVANCED_LIB})
    
    # Copy DLL to output directory
    if(WIN32)
        add_custom_command(TARGET test_advanced POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/advanced/krep_advanced.dll"
            $<TARGET_FILE_DIR:test_advanced>)
    endif()
endif()

# Simple example  
if(KREP_SIMPLE_LIB)
    add_executable(example_usage examples/example_usage.c)
    target_link_libraries(example_usage ${KREP_SIMPLE_LIB})
    
    # Copy DLL to output directory
    if(WIN32)
        add_custom_command(TARGET example_usage POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/simple/krep_simple.dll"
            $<TARGET_FILE_DIR:example_usage>)
    endif()
endif()
