cmake_minimum_required(VERSION 3.16)
project(Anyxbox LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
enable_testing()
add_library(anyxbox_loader src/loader/pe_header.cpp)
target_include_directories(anyxbox_loader PUBLIC src)
add_executable(test_pe_header tests/test_pe_header.cpp)
target_link_libraries(test_pe_header anyxbox_loader)
add_test(NAME pe_header COMMAND test_pe_header)

# Add to CMakeLists.txt
add_library(anyxbox_pe src/loader/pe_image.cpp src/syslayer/import_registry.cpp)
target_include_directories(anyxbox_pe PUBLIC src)
foreach(t pe_image import_registry)
  add_executable(test_${t} tests/test_${t}.cpp)
  target_link_libraries(test_${t} anyxbox_pe)
  add_test(NAME ${t} COMMAND test_${t})
endforeach()

add_library(anyxbox_win32 src/syslayer/win32_sync.cpp src/syslayer/win32_memory.cpp)
target_include_directories(anyxbox_win32 PUBLIC src)
find_package(Threads REQUIRED)
target_link_libraries(anyxbox_win32 PUBLIC Threads::Threads)
add_executable(test_win32_sync tests/test_win32_sync.cpp)
target_link_libraries(test_win32_sync anyxbox_win32)
add_test(NAME win32_sync COMMAND test_win32_sync)

add_library(anyxbox_sys3 src/loader/pe_report.cpp src/syslayer/win32_thread.cpp src/syslayer/win32_file.cpp)
target_link_libraries(anyxbox_sys3 PUBLIC anyxbox_pe anyxbox_win32)
add_executable(test_win32_batch3 tests/test_win32_batch3.cpp)
target_link_libraries(test_win32_batch3 anyxbox_sys3)
add_test(NAME win32_batch3 COMMAND test_win32_batch3)
add_executable(pe_probe tools/pe_probe.cpp)
target_link_libraries(pe_probe anyxbox_sys3)

add_library(anyxbox_runtime src/runtime/run_pe.cpp src/runtime/kernel32.cpp src/runtime/msvcrt.cpp)
target_link_libraries(anyxbox_runtime PUBLIC anyxbox_pe anyxbox_win32)
add_executable(test_run_pe tests/test_run_pe.cpp)
target_include_directories(test_run_pe PRIVATE tests)
target_link_libraries(test_run_pe anyxbox_runtime)
add_test(NAME run_pe COMMAND test_run_pe)

add_executable(test_kernel32 tests/test_kernel32.cpp)
target_include_directories(test_kernel32 PRIVATE tests)
target_link_libraries(test_kernel32 anyxbox_runtime)
add_test(NAME kernel32 COMMAND test_kernel32)

foreach(t protections msvcrt)
  add_executable(test_${t} tests/test_${t}.cpp)
  target_include_directories(test_${t} PRIVATE tests)
  target_link_libraries(test_${t} anyxbox_runtime)
  add_test(NAME ${t} COMMAND test_${t})
endforeach()

add_library(anyxbox_d3d11 src/gfx/d3d11_model.cpp)
target_include_directories(anyxbox_d3d11 PUBLIC src)
add_executable(test_d3d11_model tests/test_d3d11_model.cpp)
target_link_libraries(test_d3d11_model anyxbox_d3d11)
add_test(NAME d3d11_model COMMAND test_d3d11_model)
add_executable(render_triangle tools/render_triangle.cpp)
target_link_libraries(render_triangle anyxbox_d3d11)

add_library(anyxbox_vulkan src/gfx/vulkan.cpp src/gfx/render.cpp)
target_include_directories(anyxbox_vulkan PUBLIC src)
target_link_libraries(anyxbox_vulkan PUBLIC ${CMAKE_DL_LIBS})
add_executable(test_vulkan_triangle tests/test_vulkan_triangle.cpp)
target_link_libraries(test_vulkan_triangle anyxbox_vulkan anyxbox_d3d11)
add_test(NAME vulkan_triangle COMMAND test_vulkan_triangle)

add_library(anyxbox_d3d11_vulkan src/gfx/d3d11_vulkan.cpp)
target_link_libraries(anyxbox_d3d11_vulkan PUBLIC anyxbox_d3d11 anyxbox_vulkan)
add_executable(test_d3d11_vulkan tests/test_d3d11_vulkan.cpp)
target_link_libraries(test_d3d11_vulkan anyxbox_d3d11_vulkan)
add_test(NAME d3d11_vulkan COMMAND test_d3d11_vulkan)

add_library(anyxbox_shader src/shader/dxbc.cpp src/shader/dxbc_spirv.cpp)
target_include_directories(anyxbox_shader PUBLIC src)
# link test_dxbc against anyxbox_shader plus the gfx library (d3d11_model, d3d11_vulkan, vulkan, render) as test_d3d11_vulkan is
add_executable(test_dxbc tests/test_dxbc.cpp)
target_include_directories(test_dxbc PRIVATE tests)
target_link_libraries(test_dxbc anyxbox_shader anyxbox_gfx)
add_test(NAME dxbc COMMAND test_dxbc)

# no new targets: tests/test_dxbc.cpp and src/shader sources are already in the batch 10 targets; no new files.

# no new targets

# Add to CMakeLists.txt
add_executable(test_real_dxbc tests/test_real_dxbc.cpp)
target_include_directories(test_real_dxbc PRIVATE tests)
target_link_libraries(test_real_dxbc anyxbox_shader anyxbox_gfx)   # same libraries test_dxbc links
add_test(NAME real_dxbc COMMAND test_real_dxbc)

# Build fix: anyxbox_gfx was referenced by test_dxbc and test_real_dxbc but never defined.
add_library(anyxbox_gfx INTERFACE)
target_link_libraries(anyxbox_gfx INTERFACE anyxbox_d3d11_vulkan)

# Batch 16: D3D11 COM layer for guest programs
add_library(anyxbox_d3d11_rt src/runtime/d3d11_runtime.cpp)
target_link_libraries(anyxbox_d3d11_rt PUBLIC anyxbox_runtime anyxbox_shader anyxbox_d3d11)
add_executable(test_d3d11_guest tests/test_d3d11_guest.cpp)
target_include_directories(test_d3d11_guest PRIVATE tests)
target_link_libraries(test_d3d11_guest anyxbox_d3d11_rt anyxbox_gfx)
add_test(NAME d3d11_guest COMMAND test_d3d11_guest)

# Batch 17: depth/blend/cull state tests and the swap chain guest test. anyxbox_d3d11_rt already exists (batch 16).
add_executable(test_d3d11_state tests/test_d3d11_state.cpp)
target_include_directories(test_d3d11_state PRIVATE tests)
target_link_libraries(test_d3d11_state anyxbox_shader anyxbox_gfx)
add_test(NAME d3d11_state COMMAND test_d3d11_state)
add_executable(test_d3d11_guest_state tests/test_d3d11_guest_state.cpp)
target_include_directories(test_d3d11_guest_state PRIVATE tests)
target_link_libraries(test_d3d11_guest_state anyxbox_d3d11_rt anyxbox_gfx)
add_test(NAME d3d11_guest_state COMMAND test_d3d11_guest_state)

# Batch 18: textures, samplers, index buffers through the COM layer (anyxbox_d3d11_rt and anyxbox_gfx already exist)
add_executable(test_d3d11_guest_tex tests/test_d3d11_guest_tex.cpp)
target_include_directories(test_d3d11_guest_tex PRIVATE tests)
target_link_libraries(test_d3d11_guest_tex anyxbox_d3d11_rt anyxbox_gfx)
add_test(NAME d3d11_guest_tex COMMAND test_d3d11_guest_tex)

add_executable(test_real_dxbc_tex tests/test_real_dxbc_tex.cpp)
target_include_directories(test_real_dxbc_tex PRIVATE tests)
target_link_libraries(test_real_dxbc_tex PRIVATE anyxbox_shader anyxbox_gfx)

add_test(NAME test_real_dxbc_tex COMMAND test_real_dxbc_tex)

add_executable(test_d3d11_slots tests/test_d3d11_slots.cpp)
target_link_libraries(test_d3d11_slots PRIVATE anyxbox_d3d11)
add_test(NAME test_d3d11_slots COMMAND test_d3d11_slots)
add_executable(test_dxgi_factory tests/test_dxgi_factory.cpp)
target_link_libraries(test_dxgi_factory PRIVATE anyxbox_d3d11_rt)
add_test(NAME test_dxgi_factory COMMAND test_dxgi_factory)

add_executable(test_multi_tex tests/test_multi_tex.cpp)
target_include_directories(test_multi_tex PRIVATE tests)
target_link_libraries(test_multi_tex PRIVATE anyxbox_shader anyxbox_gfx)
add_test(NAME test_multi_tex COMMAND test_multi_tex)

add_library(anyxbox_user32 src/runtime/host_window.cpp src/runtime/user32.cpp)
target_include_directories(anyxbox_user32 PUBLIC src)
target_link_libraries(anyxbox_user32 PUBLIC anyxbox_loader ${CMAKE_DL_LIBS})
add_executable(test_user32 tests/test_user32.cpp)
target_link_libraries(test_user32 anyxbox_user32)
add_test(NAME user32 COMMAND test_user32)
add_executable(test_fixture_slot tests/test_fixture_slot.cpp)
target_link_libraries(test_fixture_slot anyxbox_user32 anyxbox_runtime anyxbox_d3d11_rt anyxbox_d3d11_vulkan anyxbox_vulkan anyxbox_loader)
add_test(NAME fixture_slot COMMAND test_fixture_slot ${CMAKE_SOURCE_DIR}/tests/fixtures)
add_executable(import_gap tools/import_gap.cpp)
target_link_libraries(import_gap anyxbox_user32 anyxbox_runtime anyxbox_d3d11_rt anyxbox_loader)

# new tests (user32 target and its test from batch 24 are unchanged except user32.cpp gained W variants)
add_executable(test_kernel32_extra tests/test_kernel32_extra.cpp)
target_link_libraries(test_kernel32_extra anyxbox_runtime)
add_test(NAME kernel32_extra COMMAND test_kernel32_extra)
add_executable(test_fixture_source tests/test_fixture_source.cpp)
target_include_directories(test_fixture_source PRIVATE tests tests/fixtures)
target_link_libraries(test_fixture_source anyxbox_user32 anyxbox_runtime anyxbox_d3d11_rt anyxbox_d3d11_vulkan anyxbox_vulkan anyxbox_loader)
add_test(NAME fixture_source COMMAND test_fixture_source)
add_executable(run_exe tools/run_exe.cpp)
target_link_libraries(run_exe anyxbox_user32 anyxbox_runtime anyxbox_d3d11_rt anyxbox_d3d11_vulkan anyxbox_vulkan anyxbox_loader)
add_executable(gen_fixture_shaders tools/gen_fixture_shaders.cpp)
target_include_directories(gen_fixture_shaders PRIVATE tests)

add_executable(test_crt_startup tests/test_crt_startup.cpp)
target_link_libraries(test_crt_startup anyxbox_runtime)
add_test(NAME crt_startup COMMAND test_crt_startup)

# src/runtime/teb.cpp is used by kernel32.cpp and run_pe.cpp, so it belongs to the runtime target
target_sources(anyxbox_runtime PRIVATE src/runtime/teb.cpp)
add_executable(test_teb tests/test_teb.cpp)
target_link_libraries(test_teb anyxbox_runtime)
add_test(NAME teb COMMAND test_teb)

# batch 31 (tests + tools only; no new src files)
add_executable(test_fixture_scene tests/test_fixture_scene.cpp)
target_include_directories(test_fixture_scene PRIVATE tests tests/fixtures)
target_link_libraries(test_fixture_scene PRIVATE anyxbox_user32 anyxbox_runtime anyxbox_d3d11_rt anyxbox_d3d11_vulkan anyxbox_vulkan anyxbox_loader)
add_test(NAME fixture_scene COMMAND test_fixture_scene)
# tools/run_exe.cpp now includes gfx/d3d11_vulkan.h: if run_exe is a CMake target, add anyxbox_d3d11_vulkan anyxbox_vulkan to its link libs.

# batch 32: no new tests or targets. test_fixture_slot now includes gfx/d3d11_vulkan.h: add anyxbox_d3d11_vulkan anyxbox_vulkan to its link libs if not already present.

# batch 33: no new src files
add_executable(test_tls tests/test_tls.cpp)
target_include_directories(test_tls PRIVATE tests)
target_link_libraries(test_tls PRIVATE anyxbox_runtime anyxbox_loader)
add_test(NAME tls COMMAND test_tls)

# batch 34: no new tests or src files (test_kernel32 changed; registry count now 54)

# batch 35: no new tests or src files (test_kernel32 registry count now 59)

target_sources(anyxbox_runtime PRIVATE src/runtime/devices.cpp)
# batch 36: NEW SOURCE FILE src/runtime/devices.cpp (+ devices.h). Add it to the same library that holds src/runtime/kernel32.cpp (anyxbox_runtime) if sources are listed explicitly.
add_executable(test_devices tests/test_devices.cpp)
target_include_directories(test_devices PRIVATE tests)
target_link_libraries(test_devices PRIVATE anyxbox_runtime anyxbox_loader)
add_test(NAME devices COMMAND test_devices)

target_sources(anyxbox_runtime PRIVATE src/runtime/seh.cpp)
# batch 37: NEW SOURCE FILES src/runtime/seh.cpp and seh.h. Add seh.cpp to the library that holds src/runtime/run_pe.cpp (anyxbox_runtime) if sources are listed explicitly.
# seh.cpp contains top-level assembly; it needs no extra link flags.
add_executable(test_seh tests/test_seh.cpp)
target_include_directories(test_seh PRIVATE tests)
target_link_libraries(test_seh PRIVATE anyxbox_runtime anyxbox_loader)
add_test(NAME seh COMMAND test_seh)

# batch 38: no new tests or src files (test_seh and test_protections changed; seh.cpp changed)
