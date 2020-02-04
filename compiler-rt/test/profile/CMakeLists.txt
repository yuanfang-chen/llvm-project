set(PROFILE_LIT_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(PROFILE_LIT_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR})

set(PROFILE_TEST_DEPS ${SANITIZER_COMMON_LIT_TEST_DEPS})
if(NOT COMPILER_RT_STANDALONE_BUILD)
  list(APPEND PROFILE_TEST_DEPS profile llvm-profdata llvm-cov)
endif()

set(PROFILE_TEST_ARCH ${PROFILE_SUPPORTED_ARCH})
if(APPLE)
  darwin_filter_host_archs(PROFILE_SUPPORTED_ARCH PROFILE_TEST_ARCH)
endif()

set(LIT_SITE_MULTI_CFG_PROFILE "Compiler-RT profile")
foreach(arch ${PROFILE_TEST_ARCH})
  set(PROFILE_TEST_TARGET_ARCH ${arch})
  get_test_cc_for_arch(${arch} PROFILE_TEST_TARGET_CC PROFILE_TEST_TARGET_CFLAGS)
  set(CONFIG_NAME Profile-${arch})
  configure_lit_site_cfg(
    ${CMAKE_CURRENT_SOURCE_DIR}/lit.site.cfg.py.in
    ${CMAKE_CURRENT_BINARY_DIR}/${CONFIG_NAME}/lit.site.cfg.py
   )
  string(APPEND LIT_SITE_MULTI_CFG_PROFILE
         "\n${CMAKE_CURRENT_BINARY_DIR}/${CONFIG_NAME}/lit.site.cfg.py")
endforeach()

add_lit_testsuite(check-profile "Running the profile tests"
  ${CMAKE_CURRENT_BINARY_DIR}
  DEPENDS ${PROFILE_TEST_DEPS})
set_target_properties(check-profile PROPERTIES FOLDER "Compiler-RT Misc")
configure_lit_site_multi_cfg(${LIT_SITE_MULTI_CFG_PROFILE}
                             ${CMAKE_CURRENT_BINARY_DIR}/lit.site.multi.cfg
                             ${CMAKE_CURRENT_SOURCE_DIR}/lit.cfg.py)
