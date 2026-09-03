add_executable(statewright_contract_tests
  contracts/test_canonical_json.cpp
  contracts/test_hash.cpp
  contracts/test_oracle_foundation.cpp
  contracts/test_typed_id.cpp
  common/test_utf8.cpp
  core/test_authority.cpp
  core/test_event_store.cpp
  core/test_operational_hypotheses.cpp
  core/test_transaction.cpp
  core/test_workspace.cpp
  sources/test_fetch.cpp
  sources/test_extraction.cpp
  sources/test_records.cpp
  sources/test_scheduler.cpp
  providers/test_reasoning_provider.cpp
  providers/test_subprocess_provider.cpp
  reasoning/test_adapters.cpp
  reasoning/test_benchmark.cpp
  reasoning/test_causal.cpp
  reasoning/test_hypotheses.cpp
  reasoning/test_kernel.cpp
  reasoning/test_qualification.cpp
  reasoning/test_reasoning_search.cpp
  reasoning/test_search_provider.cpp
  reasoning/test_certification.cpp
  reasoning/test_context.cpp
  reasoning/test_evaluation.cpp
  reasoning/test_generator.cpp
  reasoning/test_synthesis.cpp
  reasoning/test_synthesis_provider.cpp
  reasoning/test_topology.cpp
  reasoning/test_verification.cpp
  reasoning/test_verification_provider.cpp
  saa/test_adaptation_lineage.cpp
  saa/test_algorithm_experiment.cpp
  saa/test_algorithm_ir.cpp
  saa/test_autonomous_promotion_policy.cpp
  saa/test_dynamics.cpp
  saa/test_experiment_aggregation.cpp
  saa/test_failure_algebra.cpp
  saa/test_improvement_scheduling.cpp
  saa/test_intelligence_loop.cpp
  saa/test_knowledge_governance.cpp
  saa/test_mimo.cpp
  saa/test_multistep_evolution.cpp
  saa/test_nonlinear_control.cpp
  saa/test_nonlinear_evidence.cpp
  saa/test_nonlinear_global.cpp
  saa/test_nonlinear_geometry.cpp
  saa/test_nonlinear_jet.cpp
  saa/test_nonlinear_lie.cpp
  saa/test_nonlinear_lift.cpp
  saa/test_nonlinear_remainder.cpp
  saa/test_nonlinear_search.cpp
  saa/test_nonlinear_stability.cpp
  saa/test_nonlinear_transforms.cpp
  saa/test_normalization.cpp
  saa/test_projection.cpp
  saa/test_probation.cpp
  saa/test_representative.cpp
  saa/test_representative_form.cpp
  saa/test_reasoning_algebra.cpp
  saa/test_reasoning_composition.cpp
  saa/test_reasoning_equivalence.cpp
  saa/test_reasoning_fit.cpp
  saa/test_reasoning_outcome.cpp
  saa/test_reasoning_semantics.cpp
  saa/test_search.cpp
  saa/test_semantic.cpp
  saa/test_semantic_alignment.cpp
  saa/test_semantic_revision.cpp
  saa/test_semantic_units.cpp
  saa/test_transfer_adaptation.cpp
  saa/test_unified_retrieval.cpp
  egcf/test_registry.cpp
  egcf/test_adaptation_lineage_store.cpp
  egcf/test_approval.cpp
  egcf/test_brain_feed.cpp
  egcf/test_canonical_algorithm_store.cpp
  egcf/test_compiler.cpp
  egcf/test_context.cpp
  egcf/test_engine.cpp
  egcf/test_evidence_assurance.cpp
  egcf/test_improvement_loop_store.cpp
  egcf/test_internet_experiment.cpp
  egcf/test_internet_feed.cpp
  egcf/test_internet_improvement_store.cpp
  egcf/test_internet_reasoning.cpp
  egcf/test_knowledge_governance_store.cpp
  egcf/test_lifecycle.cpp
  egcf/test_nonlinear_store.cpp
  egcf/test_reasoning_store.cpp
  egcf/test_reasoning_service.cpp
  egcf/test_semantic_ontology.cpp
  egcf/test_simulation.cpp
  egcf/test_store.cpp
)

add_executable(statewright_fake_provider_runner
  providers/fake_subprocess_runner.cpp
)
target_link_libraries(statewright_fake_provider_runner
  PRIVATE StateWright::contracts
)
statewright_enable_warnings(statewright_fake_provider_runner)

add_executable(statewright_internet_fixture_server
  sources/internet_fixture_server.cpp
)
statewright_enable_warnings(statewright_internet_fixture_server)
target_link_libraries(statewright_contract_tests
  PRIVATE
    StateWright::core
    StateWright::sources
    StateWright::reasoning
    StateWright::saa
    StateWright::egcf
    Catch2::Catch2WithMain
)
statewright_enable_warnings(statewright_contract_tests)
target_compile_definitions(statewright_contract_tests PRIVATE
  STATEWRIGHT_ORACLE_FIXTURES="${STATEWRIGHT_ORACLE_FIXTURES}"
  STATEWRIGHT_EGCF_RECORD_FIXTURES="${PROJECT_SOURCE_DIR}/contracts/fixtures/egcf-records-v1.json"
  STATEWRIGHT_RESOURCE_ROOT="${PROJECT_SOURCE_DIR}/resources"
  STATEWRIGHT_FAKE_PROVIDER_RUNNER="$<TARGET_FILE:statewright_fake_provider_runner>"
)
add_dependencies(statewright_contract_tests statewright_fake_provider_runner)
add_test(NAME statewright_contract_tests COMMAND statewright_contract_tests)

add_test(NAME statewright_cli_version
  COMMAND statewright version --json)
set_tests_properties(statewright_cli_version PROPERTIES
  PASS_REGULAR_EXPRESSION "oracle_commit")

add_test(NAME statewright_shadow_typed_id
  COMMAND bash -c "printf '%s\\n' '{\"operation\":\"typed_id\",\"object_type\":\"Intent\",\"payload\":{\"objective\":\"inspect\"}}' | '$<TARGET_FILE:statewright-shadow>'")
set_tests_properties(statewright_shadow_typed_id PROPERTIES
  PASS_REGULAR_EXPRESSION "intent:sha256")

add_test(NAME statewright_cli_saa_canonicalize
  COMMAND statewright saa-canonicalize
    "{\"nodes\":[{\"id\":\"n\",\"primitive\":\"IDENTITY\",\"operands\":[{\"constant\":1}]}],\"outputs\":[{\"position\":0,\"source\":{\"node\":\"n\"}}]}")
set_tests_properties(statewright_cli_saa_canonicalize PROPERTIES
  PASS_REGULAR_EXPRESSION "structural_hash")

add_test(NAME statewright_cli_egcf_describe
  COMMAND statewright egcf-command-describe algorithm.search@1)
set_tests_properties(statewright_cli_egcf_describe PROPERTIES
  PASS_REGULAR_EXPRESSION "algorithm.search@1")

add_test(NAME statewright_cli_json_smoke
  COMMAND "${CMAKE_COMMAND}"
    -DSTATEWRIGHT_EXECUTABLE=$<TARGET_FILE:statewright>
    -P "${CMAKE_CURRENT_SOURCE_DIR}/cli_smoke.cmake")

add_test(NAME statewright_cli_internet_smoke
  COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/internet_cli_smoke.sh"
    "$<TARGET_FILE:statewright>"
    "$<TARGET_FILE:statewright_internet_fixture_server>"
    "${PROJECT_SOURCE_DIR}/resources/fixtures/internet/identity-v1.json")
set_tests_properties(statewright_cli_internet_smoke PROPERTIES RUN_SERIAL TRUE)

add_test(NAME statewright_saa_internet_howto_smoke
  COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/saa_internet_howto_smoke.sh"
    "${PROJECT_SOURCE_DIR}/docs/SAA_PERSISTENT_INTERNET_IMPROVEMENT_HOWTO.md"
    "$<TARGET_FILE:statewright>"
    "$<TARGET_FILE:statewright_internet_fixture_server>"
    "${PROJECT_SOURCE_DIR}/resources/fixtures/internet/identity-v1.json")
set_tests_properties(statewright_saa_internet_howto_smoke PROPERTIES
  RUN_SERIAL TRUE)

add_test(NAME statewright_package_smoke
  COMMAND "${CMAKE_COMMAND}"
    -DSTATEWRIGHT_BINARY_DIR=${CMAKE_BINARY_DIR}
    -DSTATEWRIGHT_FIXTURE_SERVER=$<TARGET_FILE:statewright_internet_fixture_server>
    -DSTATEWRIGHT_SOURCE_DIR=${PROJECT_SOURCE_DIR}
    -P "${CMAKE_CURRENT_SOURCE_DIR}/package_smoke.cmake")
set_tests_properties(statewright_package_smoke PROPERTIES RUN_SERIAL TRUE)

add_test(NAME statewright_release_inputs
  COMMAND "${PROJECT_SOURCE_DIR}/Tools/verify_release_inputs.sh")
