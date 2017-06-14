add_custom_target(ClingUnitTests)
set_target_properties(ClingUnitTests PROPERTIES FOLDER "Tests")

# FIXME: Build UnitTest.cpp as a library (fails because include paths).
# add_library(ClingUnitCommon STATIC UnitTest.cpp)
# add_dependencies(ClingUnitTests ClingUnitCommon)

# Cache current directory for -I flag in add_cling_unittest
set(CLING_UNIT_COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Common)

function(add_cling_unittest NAME)
  set(NAME "${NAME}-unit")
  add_unittest(ClingUnitTests ${NAME}
               ${CLING_UNIT_COMMON_DIR}/UnitTest.cxx
               ${ARGN})
  target_compile_options(${NAME} PUBLIC -DLLVMDIR="${LLVM_INSTALL_PREFIX}"
                                        -I${LLVM_INSTALL_PREFIX}/include
                                        -I${CLING_UNIT_COMMON_DIR})
  target_link_libraries(${NAME} clingMetaProcessor clingInterpreter clingUtils)
endfunction()

# Scan the sub-directories for anything with CMakeLists.txt
cling_list_directories(cling_unit_dirs ${CMAKE_CURRENT_SOURCE_DIR} TRUE)

# Add the tests
foreach (test_dir ${cling_unit_dirs})
  add_subdirectory(${test_dir})
endforeach()
