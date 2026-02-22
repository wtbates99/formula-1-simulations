add_library(f1sim_core STATIC
  src/track.cpp
  src/sim_core.cpp
)

target_include_directories(f1sim_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_library(f1sim_capi STATIC
  src/c_api.cpp
  src/wasm_api.cpp
)

target_link_libraries(f1sim_capi PUBLIC f1sim_core)
target_include_directories(f1sim_capi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(F1SIM_BUILD_CLI)
  add_executable(f1sim_cli src/main_cli.cpp)
  target_link_libraries(f1sim_cli PRIVATE f1sim_capi)
endif()

if(F1SIM_BUILD_WASM)
  add_executable(f1sim_wasm
    src/track.cpp
    src/sim_core.cpp
    src/c_api.cpp
    src/wasm_api.cpp
  )
  target_include_directories(f1sim_wasm PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  set_target_properties(f1sim_wasm PROPERTIES OUTPUT_NAME "f1sim")

  target_link_options(f1sim_wasm PRIVATE
    "-O3"
    "-sMODULARIZE=1"
    "-sEXPORT_ES6=1"
    "-sEXPORT_NAME=F1SimModule"
    "-sENVIRONMENT=web"
    "-sFILESYSTEM=0"
    "-sALLOW_MEMORY_GROWTH=0"
    "-sINITIAL_MEMORY=134217728"
    "-sEXPORTED_RUNTIME_METHODS=['HEAPF32','HEAPU32']"
    "-sEXPORTED_FUNCTIONS=['_malloc','_free','_init_sim','_reset_sim','_set_controls','_step_sim','_get_vehicle_state','_run_lap','_state_x_ptr','_state_y_ptr','_state_yaw_ptr','_state_speed_ptr','_state_car_count','_f1sim_api_version','_f1sim_default_track_config','_f1sim_default_car_config','_f1sim_default_sim_config','_f1sim_create','_f1sim_destroy','_f1sim_set_car_count','_f1sim_reset','_f1sim_step','_f1sim_start_replay_capture','_f1sim_stop_replay_capture','_f1sim_replay_captured_deterministic','_f1sim_run_batch_laps','_f1sim_snapshot','_f1sim_state_speed_ptr','_f1sim_state_x_ptr','_f1sim_state_y_ptr','_f1sim_state_yaw_ptr','_f1sim_state_s_ptr','_f1sim_car_count']"
  )
endif()
