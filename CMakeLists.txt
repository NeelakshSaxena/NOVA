cmake_minimum_required(VERSION 3.16)
project(nova VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(nova_core src/cli.cpp src/lexer.cpp src/ast.cpp src/parser.cpp src/semantic.cpp src/tac.cpp src/interp.cpp src/optimizer.cpp
  src/bench/util.cpp src/bench/sha256.cpp src/bench/json.cpp src/bench/stats.cpp src/bench/process.cpp
  src/bench/env.cpp src/bench/registry.cpp src/bench/records.cpp src/bench/harness.cpp src/bench/cli.cpp
  src/tune/space.cpp src/tune/features.cpp src/tune/adaptive.cpp src/tune/tuner.cpp src/tune/evaluate.cpp src/tune/study.cpp src/tune/cli.cpp)
target_include_directories(nova_core PUBLIC include)
if(WIN32)
  target_link_libraries(nova_core PUBLIC advapi32)
endif()
target_compile_definitions(nova_core PUBLIC NOVA_VERSION="${PROJECT_VERSION}")
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(nova_core PUBLIC -Wall -Wextra)
endif()

add_executable(nova src/main.cpp)
target_link_libraries(nova PRIVATE nova_core)

enable_testing()

add_executable(test_cli tests/test_cli.cpp)
target_link_libraries(test_cli PRIVATE nova_core)
target_compile_definitions(test_cli PRIVATE NOVA_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME cli_unit COMMAND test_cli)

add_executable(test_lexer tests/test_lexer.cpp)
target_link_libraries(test_lexer PRIVATE nova_core)
add_test(NAME lexer_unit COMMAND test_lexer)

add_executable(test_parser tests/test_parser.cpp)
target_link_libraries(test_parser PRIVATE nova_core)
add_test(NAME parser_unit COMMAND test_parser)

add_executable(test_semantic tests/test_semantic.cpp)
target_link_libraries(test_semantic PRIVATE nova_core)
add_test(NAME semantic_unit COMMAND test_semantic)

add_executable(test_tac tests/test_tac.cpp)
target_link_libraries(test_tac PRIVATE nova_core)
add_test(NAME tac_unit COMMAND test_tac)

add_executable(helper_child tests/helper_child.cpp)

add_executable(test_shuffle_checked tests/test_shuffle_checked.cpp)
target_include_directories(test_shuffle_checked PRIVATE include)
target_compile_definitions(test_shuffle_checked PRIVATE _GLIBCXX_DEBUG _GLIBCXX_ASSERTIONS)
add_test(NAME shuffle_checked COMMAND test_shuffle_checked)

add_executable(test_bench_unit tests/test_bench_unit.cpp)
target_link_libraries(test_bench_unit PRIVATE nova_core)
add_test(NAME bench_unit COMMAND test_bench_unit)

add_executable(test_bench_process tests/test_bench_process.cpp)
target_link_libraries(test_bench_process PRIVATE nova_core)
target_compile_definitions(test_bench_process PRIVATE
  NOVA_HELPER_CHILD="$<TARGET_FILE:helper_child>" NOVA_TEST_TMP_DIR="${CMAKE_BINARY_DIR}/test_tmp")
add_dependencies(test_bench_process helper_child)
add_test(NAME bench_process COMMAND test_bench_process)

add_executable(test_bench_integration tests/test_bench_integration.cpp)
target_link_libraries(test_bench_integration PRIVATE nova_core)
target_compile_definitions(test_bench_integration PRIVATE
  NOVA_SOURCE_DIR="${CMAKE_SOURCE_DIR}" NOVA_TEST_COMPILER="${CMAKE_CXX_COMPILER}"
  NOVA_TEST_TMP_DIR="${CMAKE_BINARY_DIR}/test_tmp")
add_test(NAME bench_integration COMMAND test_bench_integration)
set_tests_properties(bench_integration PROPERTIES TIMEOUT 900)

add_executable(test_tune_unit tests/test_tune_unit.cpp)
target_link_libraries(test_tune_unit PRIVATE nova_core)
add_test(NAME tune_unit COMMAND test_tune_unit)

add_executable(test_adaptive_unit tests/test_adaptive_unit.cpp)
target_link_libraries(test_adaptive_unit PRIVATE nova_core)
target_compile_definitions(test_adaptive_unit PRIVATE NOVA_TEST_TMP_DIR="${CMAKE_BINARY_DIR}/test_tmp")
add_test(NAME adaptive_unit COMMAND test_adaptive_unit)

add_executable(test_study_unit tests/test_study_unit.cpp)
target_link_libraries(test_study_unit PRIVATE nova_core)
add_test(NAME study_unit COMMAND test_study_unit)

add_executable(test_adaptive_integration tests/test_adaptive_integration.cpp)
target_link_libraries(test_adaptive_integration PRIVATE nova_core)
target_compile_definitions(test_adaptive_integration PRIVATE
  NOVA_SOURCE_DIR="${CMAKE_SOURCE_DIR}" NOVA_TEST_COMPILER="${CMAKE_CXX_COMPILER}"
  NOVA_TEST_TMP_DIR="${CMAKE_BINARY_DIR}/test_tmp")
add_test(NAME adaptive_integration COMMAND test_adaptive_integration)
set_tests_properties(adaptive_integration PROPERTIES TIMEOUT 1800)

add_executable(test_tune_integration tests/test_tune_integration.cpp)
target_link_libraries(test_tune_integration PRIVATE nova_core)
target_compile_definitions(test_tune_integration PRIVATE
  NOVA_SOURCE_DIR="${CMAKE_SOURCE_DIR}" NOVA_TEST_COMPILER="${CMAKE_CXX_COMPILER}"
  NOVA_TEST_TMP_DIR="${CMAKE_BINARY_DIR}/test_tmp")
add_test(NAME tune_integration COMMAND test_tune_integration)
set_tests_properties(tune_integration PROPERTIES TIMEOUT 1500)

add_executable(test_optimizer tests/test_optimizer.cpp)
target_link_libraries(test_optimizer PRIVATE nova_core)
add_test(NAME optimizer_unit COMMAND test_optimizer)

# End-to-end checks against the real executable.
add_test(NAME cli_help COMMAND nova --help)
set_tests_properties(cli_help PROPERTIES PASS_REGULAR_EXPRESSION "Usage: nova")

add_test(NAME cli_version COMMAND nova --version)
set_tests_properties(cli_version PROPERTIES PASS_REGULAR_EXPRESSION "^nova ${PROJECT_VERSION}")

add_test(NAME cli_unknown_option COMMAND nova --bogus)
set_tests_properties(cli_unknown_option PROPERTIES WILL_FAIL TRUE)

# Token dump end-to-end.
add_test(NAME cli_tokens_basic COMMAND nova tokens ${CMAKE_SOURCE_DIR}/examples/basic.nova)
set_tests_properties(cli_tokens_basic PROPERTIES PASS_REGULAR_EXPRESSION "1:1 KW_LET 'let'.*4:8 SEMICOLON '.'")

# Invalid character: exit code must be non-zero ...
add_test(NAME cli_tokens_invalid_exit COMMAND nova tokens ${CMAKE_SOURCE_DIR}/examples/invalid_char.nova)
set_tests_properties(cli_tokens_invalid_exit PROPERTIES WILL_FAIL TRUE)
# ... and the diagnostic must carry the exact location.
add_test(NAME cli_tokens_invalid_location COMMAND nova tokens ${CMAKE_SOURCE_DIR}/examples/invalid_char.nova)
set_tests_properties(cli_tokens_invalid_location PROPERTIES
  PASS_REGULAR_EXPRESSION "invalid_char.nova:2:11: error: unexpected character '@'")

# NOTE: regexes must not contain ';' (CMake splits property values on it, which makes a regex vacuous).
# AST dump end-to-end.
add_test(NAME cli_ast_basic COMMAND nova ast ${CMAKE_SOURCE_DIR}/examples/basic.nova)
set_tests_properties(cli_ast_basic PROPERTIES PASS_REGULAR_EXPRESSION
  "Program.*Let z.*Binary \\+.*Binary \\*.*Variable x.*Integer 2.*Variable y.*Print.*Variable z")

# Syntax error: non-zero exit ...
add_test(NAME cli_ast_invalid_exit COMMAND nova ast ${CMAKE_SOURCE_DIR}/examples/invalid_syntax.nova)
set_tests_properties(cli_ast_invalid_exit PROPERTIES WILL_FAIL TRUE)
# ... with an exact location and message.
add_test(NAME cli_ast_invalid_location COMMAND nova ast ${CMAKE_SOURCE_DIR}/examples/invalid_syntax.nova)
set_tests_properties(cli_ast_invalid_location PROPERTIES
  PASS_REGULAR_EXPRESSION "invalid_syntax.nova:2:14: error: expected expression, found '.'")

# Lexical errors are reported (not parsed) by `ast` too.
add_test(NAME cli_ast_lexical_error COMMAND nova ast ${CMAKE_SOURCE_DIR}/examples/invalid_char.nova)
set_tests_properties(cli_ast_lexical_error PROPERTIES
  PASS_REGULAR_EXPRESSION "invalid_char.nova:2:11: error: unexpected character '@'")

# TAC / run end-to-end (regexes must not contain ';').
add_test(NAME cli_tac_basic COMMAND nova tac ${CMAKE_SOURCE_DIR}/examples/basic.nova)
set_tests_properties(cli_tac_basic PROPERTIES PASS_REGULAR_EXPRESSION
  "x = 10.*y = 20.*[$]t1 = x [*] 2.*z = [$]t1 [+] y.*print z")

add_test(NAME cli_tac_undeclared_exit COMMAND nova tac ${CMAKE_SOURCE_DIR}/examples/undeclared.nova)
set_tests_properties(cli_tac_undeclared_exit PROPERTIES WILL_FAIL TRUE)
add_test(NAME cli_tac_undeclared_msg COMMAND nova tac ${CMAKE_SOURCE_DIR}/examples/undeclared.nova)
set_tests_properties(cli_tac_undeclared_msg PROPERTIES PASS_REGULAR_EXPRESSION
  "undeclared.nova:2:7: error: use of undeclared variable 'y'")

add_test(NAME cli_tac_duplicate_msg COMMAND nova tac ${CMAKE_SOURCE_DIR}/examples/duplicate.nova)
set_tests_properties(cli_tac_duplicate_msg PROPERTIES PASS_REGULAR_EXPRESSION
  "duplicate.nova:2:5: error: variable 'x' is already declared [(]first declared at 1:5[)]")

add_test(NAME cli_run_basic COMMAND nova run ${CMAKE_SOURCE_DIR}/examples/basic.nova)
set_tests_properties(cli_run_basic PROPERTIES PASS_REGULAR_EXPRESSION "^40")

add_test(NAME cli_run_divzero_exit COMMAND nova run ${CMAKE_SOURCE_DIR}/examples/divzero.nova)
set_tests_properties(cli_run_divzero_exit PROPERTIES WILL_FAIL TRUE)
add_test(NAME cli_run_divzero_msg COMMAND nova run ${CMAKE_SOURCE_DIR}/examples/divzero.nova)
set_tests_properties(cli_run_divzero_msg PROPERTIES PASS_REGULAR_EXPRESSION
  "divzero.nova:4:9: error: runtime error: division by zero")

# Optimizer end-to-end (regexes must not contain ';').
add_test(NAME cli_opt_showall COMMAND nova opt ${CMAKE_SOURCE_DIR}/examples/optimize_demo.nova --show-all)
set_tests_properties(cli_opt_showall PROPERTIES PASS_REGULAR_EXPRESSION
  "== original ==.*== after fold.*== after dce.*== final .fixed point after 3 iterations. ==.*print 8.*print 28")

add_test(NAME cli_opt_bad_pass COMMAND nova opt ${CMAKE_SOURCE_DIR}/examples/optimize_demo.nova --passes=bogus)
set_tests_properties(cli_opt_bad_pass PROPERTIES WILL_FAIL TRUE)

# Benchmark lab CLI (regexes must not contain ';').
add_test(NAME cli_bench_list COMMAND nova bench list)
set_tests_properties(cli_bench_list PROPERTIES PASS_REGULAR_EXPRESSION
  "matmul.*tuning sizes: 256, 384.*holdout sizes: 320, 448.*vector_add.*stencil")

add_test(NAME cli_bench_report_saved
  COMMAND nova bench report ${CMAKE_SOURCE_DIR}/benchmarks/results/V0.6-baseline-pinned)
set_tests_properties(cli_bench_report_saved PROPERTIES PASS_REGULAR_EXPRESSION
  "Intel.*matmul  n=256.*PASS.*stencil  n=800000.*vector_add  n=4000000")

add_test(NAME cli_bench_report_missing_dir COMMAND nova bench report ${CMAKE_SOURCE_DIR}/benchmarks/results/does-not-exist)
set_tests_properties(cli_bench_report_missing_dir PROPERTIES WILL_FAIL TRUE)

add_test(NAME cli_bench_bad_option COMMAND nova bench run --bogus=1)
set_tests_properties(cli_bench_bad_option PROPERTIES WILL_FAIL TRUE)

# SmartPass CLI (regexes must not contain ';').
add_test(NAME cli_tune_missing_budget COMMAND nova tune stencil)
set_tests_properties(cli_tune_missing_budget PROPERTIES WILL_FAIL TRUE)
add_test(NAME cli_tune_missing_budget_msg COMMAND nova tune stencil)
set_tests_properties(cli_tune_missing_budget_msg PROPERTIES PASS_REGULAR_EXPRESSION "requires --budget=B")

add_test(NAME cli_tune_unknown_strategy COMMAND nova tune stencil --budget=1 --strategy=bayesian)
set_tests_properties(cli_tune_unknown_strategy PROPERTIES WILL_FAIL TRUE)

add_test(NAME cli_tune_holdout_refused_msg COMMAND nova tune matmul --budget=1 --size=320)
set_tests_properties(cli_tune_holdout_refused_msg PROPERTIES PASS_REGULAR_EXPRESSION "reserved holdout size")
add_test(NAME cli_tune_holdout_refused_exit COMMAND nova tune matmul --budget=1 --size=320)
set_tests_properties(cli_tune_holdout_refused_exit PROPERTIES WILL_FAIL TRUE)

# V0.8 CLI (regexes must not contain ';').
add_test(NAME cli_tune_bad_ablation COMMAND nova tune stencil --budget=1 --strategy=adaptive --ablation=bogus)
set_tests_properties(cli_tune_bad_ablation PROPERTIES WILL_FAIL TRUE)
add_test(NAME cli_tune_record_history_needs_file COMMAND nova tune stencil --budget=1 --strategy=adaptive --record-history)
set_tests_properties(cli_tune_record_history_needs_file PROPERTIES WILL_FAIL TRUE)
add_test(NAME cli_study_saved_tables COMMAND nova study
  --tables=${CMAKE_SOURCE_DIR}/benchmarks/results/V0.8-table-stencil,${CMAKE_SOURCE_DIR}/benchmarks/results/V0.8-table-vector_add,${CMAKE_SOURCE_DIR}/benchmarks/results/V0.8-table-matmul
  --out=${CMAKE_BINARY_DIR}/test_tmp/cli-study --seeds=2 --budgets=1,4)
set_tests_properties(cli_study_saved_tables PROPERTIES PASS_REGULAR_EXPRESSION "12 tables")
add_test(NAME cli_study_missing_args COMMAND nova study)
set_tests_properties(cli_study_missing_args PROPERTIES WILL_FAIL TRUE)
add_test(NAME cli_explain_adaptive_saved COMMAND nova explain ${CMAKE_SOURCE_DIR}/benchmarks/results/V0.8-real-stencil-adaptive-s1-B6)
set_tests_properties(cli_explain_adaptive_saved PROPERTIES PASS_REGULAR_EXPRESSION "strategy adaptive.*WINNER")

# Report generator tests (need a Python 3 interpreter; skipped with a message if none is found).
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  add_test(NAME report_generation COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/report/test_make_report.py)
  set_tests_properties(report_generation PROPERTIES TIMEOUT 600)
else()
  message(STATUS "Python 3 not found: report_generation test not registered")
endif()
