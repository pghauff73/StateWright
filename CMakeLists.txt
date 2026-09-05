cmake_minimum_required(VERSION 3.28)

project(
  StateWright
  VERSION 0.1.0
  DESCRIPTION "Governed reasoning and engineering command core"
  LANGUAGES CXX
)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(STATEWRIGHT_BUILD_TESTS "Build StateWright tests" ON)
option(STATEWRIGHT_ENABLE_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)
option(STATEWRIGHT_ENABLE_COVERAGE "Enable compiler coverage instrumentation" OFF)

set(STATEWRIGHT_ORACLE_COMMIT
    "957111e2d5d11dec719c7f993f51644e701fc256"
    CACHE STRING "Frozen OIEC Python oracle commit")

set(STATEWRIGHT_ORACLE_FIXTURES
    "${CMAKE_CURRENT_SOURCE_DIR}/contracts/fixtures/foundation-v1.json")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Dependencies)
include(Warnings)

find_package(OpenSSL 3 REQUIRED COMPONENTS Crypto)
find_package(SQLite3 REQUIRED)
find_package(Threads REQUIRED)
find_package(CURL REQUIRED)
find_library(GMP_LIBRARY NAMES gmp REQUIRED)
find_library(GMPXX_LIBRARY NAMES gmpxx REQUIRED)

if(STATEWRIGHT_ENABLE_SANITIZERS)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    message(FATAL_ERROR "Sanitizers are supported only with GCC or Clang")
  endif()
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
endif()

if(STATEWRIGHT_ENABLE_COVERAGE)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    message(FATAL_ERROR "Coverage is supported only with GCC or Clang")
  endif()
  add_compile_options(--coverage -O0 -g)
  add_link_options(--coverage)
endif()

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/build_info.hpp.in"
  "${CMAKE_CURRENT_BINARY_DIR}/generated/statewright/contracts/build_info.hpp"
  @ONLY
)

add_library(statewright_common STATIC
  Core/src/common/utf8.cpp
)
add_library(StateWright::common ALIAS statewright_common)
target_include_directories(statewright_common
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Core/include>"
    "$<INSTALL_INTERFACE:include>"
)
statewright_enable_warnings(statewright_common)

add_library(statewright_contracts STATIC
  Core/src/contracts/build_identity.cpp
  Core/src/contracts/canonical_json.cpp
  Core/src/contracts/hash.cpp
  Core/src/contracts/typed_id.cpp
)
add_library(StateWright::contracts ALIAS statewright_contracts)
target_include_directories(statewright_contracts
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Core/include>"
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>"
    "$<INSTALL_INTERFACE:include>"
)
target_link_libraries(statewright_contracts
  PUBLIC
    StateWright::common
    nlohmann_json::nlohmann_json
    OpenSSL::Crypto
)
statewright_enable_warnings(statewright_contracts)

add_library(statewright_core STATIC
  Core/src/core/authority.cpp
  Core/src/core/event_store.cpp
  Core/src/core/file_io.cpp
  Core/src/core/operational_hypotheses.cpp
  Core/src/core/transaction.cpp
  Core/src/core/workspace.cpp
)
add_library(StateWright::core ALIAS statewright_core)
target_include_directories(statewright_core
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Core/include>"
    "$<INSTALL_INTERFACE:include>"
)
target_link_libraries(statewright_core PUBLIC StateWright::contracts)
statewright_enable_warnings(statewright_core)

add_library(statewright_sources STATIC
  Core/src/sources/curl_http_provider.cpp
  Core/src/sources/extraction.cpp
  Core/src/sources/fetch.cpp
  Core/src/sources/policy.cpp
  Core/src/sources/records.cpp
  Core/src/sources/scheduler.cpp
  Core/src/sources/snapshot.cpp
  Core/src/sources/watchlist.cpp
)
add_library(StateWright::sources ALIAS statewright_sources)
target_include_directories(statewright_sources
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Core/include>"
    "$<INSTALL_INTERFACE:include>"
)
target_link_libraries(statewright_sources
  PUBLIC StateWright::core CURL::libcurl
  PRIVATE Threads::Threads
)
statewright_enable_warnings(statewright_sources)

add_library(statewright_providers STATIC
  Core/src/providers/reasoning_provider.cpp
  Core/src/providers/subprocess_provider.cpp
)
add_library(StateWright::providers ALIAS statewright_providers)
target_include_directories(statewright_providers
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Core/include>"
    "$<INSTALL_INTERFACE:include>"
)
target_link_libraries(statewright_providers
  PUBLIC StateWright::contracts
  PRIVATE Threads::Threads
)
statewright_enable_warnings(statewright_providers)

add_library(statewright_reasoning STATIC
  Core/src/reasoning/ablation.cpp
  Core/src/reasoning/adapters.cpp
  Core/src/reasoning/benchmark.cpp
  Core/src/reasoning/causal.cpp
  Core/src/reasoning/certification.cpp
  Core/src/reasoning/context.cpp
  Core/src/reasoning/evaluation.cpp
  Core/src/reasoning/generator.cpp
  Core/src/reasoning/hypotheses.cpp
  Core/src/reasoning/kernel.cpp
  Core/src/reasoning/qualification.cpp
  Core/src/reasoning/search.cpp
  Core/src/reasoning/search_provider.cpp
  Core/src/reasoning/synthesis.cpp
  Core/src/reasoning/synthesis_provider.cpp
  Core/src/reasoning/topology.cpp
  Core/src/reasoning/verification.cpp
  Core/src/reasoning/verification_provider.cpp
)
add_library(StateWright::reasoning ALIAS statewright_reasoning)
target_include_directories(statewright_reasoning
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Core/include>"
    "$<INSTALL_INTERFACE:include>"
)
target_link_libraries(statewright_reasoning
  PUBLIC StateWright::contracts StateWright::providers
  PRIVATE "${GMPXX_LIBRARY}" "${GMP_LIBRARY}"
)
statewright_enable_warnings(statewright_reasoning)

add_library(statewright_saa STATIC
  Core/src/saa/adaptation_lineage.cpp
  Core/src/saa/algorithm_adaptation.cpp
  Core/src/saa/algorithm_experiment.cpp
  Core/src/saa/algorithm_transfer.cpp
  Core/src/saa/algorithm_ir.cpp
  Core/src/saa/autonomous_promotion_policy.cpp
  Core/src/saa/dynamics.cpp
  Core/src/saa/experiment_aggregation.cpp
  Core/src/saa/failure_algebra.cpp
  Core/src/saa/improvement_scheduling.cpp
  Core/src/saa/intelligence_loop.cpp
  Core/src/saa/knowledge_integrity.cpp
  Core/src/saa/mimo.cpp
  Core/src/saa/multistep_evolution.cpp
  Core/src/saa/nonlinear_control.cpp
  Core/src/saa/nonlinear_evidence.cpp
  Core/src/saa/nonlinear_global.cpp
  Core/src/saa/nonlinear_geometry.cpp
  Core/src/saa/nonlinear_jet.cpp
  Core/src/saa/nonlinear_lift.cpp
  Core/src/saa/nonlinear_lie.cpp
  Core/src/saa/nonlinear_remainder.cpp
  Core/src/saa/nonlinear_search.cpp
  Core/src/saa/nonlinear_stability.cpp
  Core/src/saa/nonlinear_transforms.cpp
  Core/src/saa/normalization.cpp
  Core/src/saa/oiec_bench_gate.cpp
  Core/src/saa/projection.cpp
  Core/src/saa/probation.cpp
  Core/src/saa/promotion_governance.cpp
  Core/src/saa/representative.cpp
  Core/src/saa/representative_form.cpp
  Core/src/saa/reasoning_algebra.cpp
  Core/src/saa/reasoning_composition.cpp
  Core/src/saa/reasoning_equivalence.cpp
  Core/src/saa/reasoning_fit.cpp
  Core/src/saa/reasoning_outcome.cpp
  Core/src/saa/reasoning_semantics.cpp
  Core/src/saa/retrieval_explanation.cpp
  Core/src/saa/retrieve_first.cpp
  Core/src/saa/search.cpp
  Core/src/saa/semantic.cpp
  Core/src/saa/semantic_alignment.cpp
  Core/src/saa/semantic_revision.cpp
  Core/src/saa/semantic_units.cpp
  Core/src/saa/unified_retrieval.cpp
)
add_library(StateWright::saa ALIAS statewright_saa)
target_include_directories(statewright_saa
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Core/include>"
    "$<INSTALL_INTERFACE:include>"
)
target_link_libraries(statewright_saa
  PUBLIC StateWright::contracts SQLite3::SQLite3
         "${GMPXX_LIBRARY}" "${GMP_LIBRARY}"
)
statewright_enable_warnings(statewright_saa)

add_library(statewright_egcf STATIC
  Core/src/egcf/adaptation_lineage_store.cpp
  Core/src/egcf/approval.cpp
  Core/src/egcf/assurance.cpp
  Core/src/egcf/autonomous_promotion.cpp
  Core/src/egcf/brain_feed.cpp
  Core/src/egcf/canonical_algorithm_store.cpp
  Core/src/egcf/compiler.cpp
  Core/src/egcf/context.cpp
  Core/src/egcf/engine.cpp
  Core/src/egcf/evidence.cpp
  Core/src/egcf/execution.cpp
  Core/src/egcf/governance.cpp
  Core/src/egcf/improvement_loop_store.cpp
  Core/src/egcf/internet_experiment.cpp
  Core/src/egcf/internet_improvement_director.cpp
  Core/src/egcf/internet_improvement_orchestrator.cpp
  Core/src/egcf/internet_improvement_supervisor.cpp
  Core/src/egcf/internet_improvement_store.cpp
  Core/src/egcf/internet_orchestration_records.cpp
  Core/src/egcf/internet_probation.cpp
  Core/src/egcf/internet_records.cpp
  Core/src/egcf/internet_feed.cpp
  Core/src/egcf/internet_reasoning.cpp
  Core/src/egcf/internet_source_coordinator.cpp
  Core/src/egcf/internet_metrics.cpp
  Core/src/egcf/knowledge_governance_store.cpp
  Core/src/egcf/lifecycle.cpp
  Core/src/egcf/nonlinear_store.cpp
  Core/src/egcf/reasoning_service.cpp
  Core/src/egcf/reasoning_store.cpp
  Core/src/egcf/records.cpp
  Core/src/egcf/registry.cpp
  Core/src/egcf/semantic_ontology.cpp
  Core/src/egcf/simulation.cpp
  Core/src/egcf/store.cpp
  Core/src/egcf/workflow.cpp
)
add_library(StateWright::egcf ALIAS statewright_egcf)
target_include_directories(statewright_egcf
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Core/include>"
    "$<INSTALL_INTERFACE:include>"
)
target_link_libraries(statewright_egcf
  PUBLIC StateWright::saa StateWright::core StateWright::reasoning StateWright::sources SQLite3::SQLite3
)
statewright_enable_warnings(statewright_egcf)

add_executable(statewright
  Apps/statewright/cli.cpp
  Apps/statewright/main.cpp
)
target_link_libraries(statewright PRIVATE StateWright::core StateWright::reasoning StateWright::saa StateWright::egcf)
target_compile_definitions(statewright PRIVATE
  STATEWRIGHT_BUILD_BINARY_ROOT="${CMAKE_CURRENT_BINARY_DIR}"
  STATEWRIGHT_BUILD_RESOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/resources"
  STATEWRIGHT_INSTALL_RESOURCE_SUBDIR="${CMAKE_INSTALL_DATADIR}/statewright"
)
statewright_enable_warnings(statewright)

add_executable(statewright-shadow Apps/statewright-shadow/main.cpp)
target_link_libraries(statewright-shadow PRIVATE StateWright::contracts)
statewright_enable_warnings(statewright-shadow)

add_executable(statewright-internet-supervisor
  Apps/statewright-internet-supervisor/main.cpp
)
target_link_libraries(statewright-internet-supervisor
  PRIVATE StateWright::egcf StateWright::contracts
)
statewright_enable_warnings(statewright-internet-supervisor)

add_executable(statewright_internet_history_benchmark
  Tools/benchmark_internet_history.cpp
)
target_link_libraries(statewright_internet_history_benchmark PRIVATE StateWright::egcf)
statewright_enable_warnings(statewright_internet_history_benchmark)

add_executable(statewright_feed_watchlist_once Tools/feed_internet_watchlist_once.cpp)
target_link_libraries(statewright_feed_watchlist_once PRIVATE StateWright::egcf)
statewright_enable_warnings(statewright_feed_watchlist_once)

add_executable(statewright_import_mathematical_acquisitions Tools/import_mathematical_acquisitions.cpp)
target_link_libraries(statewright_import_mathematical_acquisitions PRIVATE StateWright::egcf)
statewright_enable_warnings(statewright_import_mathematical_acquisitions)

install(TARGETS statewright statewright-shadow statewright-internet-supervisor
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
install(DIRECTORY resources/
  DESTINATION "${CMAKE_INSTALL_DATADIR}/statewright"
)
install(FILES IMPLEMENTATION_PLAN.md README.md
  DESTINATION "${CMAKE_INSTALL_DOCDIR}"
)
install(DIRECTORY docs/
  DESTINATION "${CMAKE_INSTALL_DOCDIR}"
)
install(DIRECTORY contracts/fixtures/
  DESTINATION "${CMAKE_INSTALL_DATADIR}/statewright/contracts/fixtures"
)
install(DIRECTORY contracts/migrations/
  DESTINATION "${CMAKE_INSTALL_DATADIR}/statewright/contracts/migrations"
)
install(FILES
  contracts/oracle/source.lock.json
  contracts/oracle/source_manifest.json
  contracts/oracle/source_manifest.sha256
  DESTINATION "${CMAKE_INSTALL_DATADIR}/statewright/contracts/oracle"
)

if(STATEWRIGHT_BUILD_TESTS)
  include(CTest)
  enable_testing()
  statewright_resolve_catch2()
  add_subdirectory(Tests)
endif()

set(CPACK_PACKAGE_NAME "StateWright")
set(CPACK_PACKAGE_VENDOR "StateWright")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_GENERATOR "TGZ")
include(CPack)
