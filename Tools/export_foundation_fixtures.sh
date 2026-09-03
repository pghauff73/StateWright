#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
lock_file="$root/contracts/oracle/source.lock.json"
oracle_repo=${STATEWRIGHT_ORACLE_REPOSITORY:-$(jq -r '.oracle_repository' "$lock_file")}
oracle_commit=$(jq -r '.oracle_commit' "$lock_file")
output="$root/contracts/fixtures/foundation-v1.json"

temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT
git -C "$oracle_repo" show "$oracle_commit:ourd/egcf/ids.py" >"$temporary/ids.py"
mkdir -p "$temporary/ourd"
git -C "$oracle_repo" show "$oracle_commit:ourd/errors.py" >"$temporary/ourd/errors.py"
git -C "$oracle_repo" show "$oracle_commit:ourd/constants.py" >"$temporary/ourd/constants.py"
git -C "$oracle_repo" show "$oracle_commit:ourd/workspace.py" >"$temporary/ourd/workspace.py"
git -C "$oracle_repo" show "$oracle_commit:ourd/models.py" >"$temporary/ourd/models.py"
git -C "$oracle_repo" show "$oracle_commit:ourd/oiec.py" >"$temporary/ourd/oiec.py"
git -C "$oracle_repo" show "$oracle_commit:ourd/authority.py" >"$temporary/ourd/authority.py"
git -C "$oracle_repo" show "$oracle_commit:ourd/persistence.py" >"$temporary/ourd/persistence.py"
git -C "$oracle_repo" show "$oracle_commit:ourd/hypotheses.py" >"$temporary/ourd/hypotheses.py"
mkdir -p "$temporary/ourd/reasoning"
git -C "$oracle_repo" show "$oracle_commit:ourd/reasoning/models.py" >"$temporary/ourd/reasoning/models.py"
git -C "$oracle_repo" show "$oracle_commit:ourd/reasoning/hypotheses.py" >"$temporary/ourd/reasoning/hypotheses.py"
git -C "$oracle_repo" show "$oracle_commit:ourd/reasoning/topology.py" >"$temporary/ourd/reasoning/topology.py"
for module_name in budget scoring diversity contradictions; do
  git -C "$oracle_repo" show "$oracle_commit:ourd/reasoning/$module_name.py" \
    >"$temporary/ourd/reasoning/$module_name.py"
done
for module_name in generator verifier falsifier; do
  git -C "$oracle_repo" show "$oracle_commit:ourd/reasoning/$module_name.py" \
    >"$temporary/ourd/reasoning/$module_name.py"
done
for module_name in ablation adapters causal context synthesis search kernel; do
  git -C "$oracle_repo" show "$oracle_commit:ourd/reasoning/$module_name.py" \
    >"$temporary/ourd/reasoning/$module_name.py"
done
mkdir -p "$temporary/ourd/egcf/algebra"
git -C "$oracle_repo" show "$oracle_commit:ourd/egcf/errors.py" >"$temporary/ourd/egcf/errors.py"
git -C "$oracle_repo" show "$oracle_commit:ourd/egcf/ids.py" >"$temporary/ourd/egcf/ids.py"
git -C "$oracle_repo" show "$oracle_commit:ourd/egcf/models.py" >"$temporary/ourd/egcf/models.py"
git -C "$oracle_repo" show "$oracle_commit:ourd/egcf/catalog.py" >"$temporary/ourd/egcf/catalog.py"
for module_name in primitives models graph ir; do
  git -C "$oracle_repo" show "$oracle_commit:ourd/egcf/algebra/$module_name.py" \
    >"$temporary/ourd/egcf/algebra/$module_name.py"
done
mkdir -p "$(dirname "$output")"

ORACLE_IDS="$temporary/ids.py" ORACLE_COMMIT="$oracle_commit" OUTPUT="$output" \
python - <<'PY'
import importlib.util
import json
import os
import sys
import tempfile
import types
from pathlib import Path

spec = importlib.util.spec_from_file_location("statewright_oracle_ids", os.environ["ORACLE_IDS"])
module = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(module)

package = types.ModuleType("ourd")
package.__path__ = [str(Path(os.environ["ORACLE_IDS"]).parent / "ourd")]
sys.modules["ourd"] = package
reasoning_package = types.ModuleType("ourd.reasoning")
reasoning_package.__path__ = [str(Path(package.__path__[0]) / "reasoning")]
sys.modules["ourd.reasoning"] = reasoning_package
egcf_package = types.ModuleType("ourd.egcf")
egcf_package.__path__ = [str(Path(package.__path__[0]) / "egcf")]
sys.modules["ourd.egcf"] = egcf_package
algebra_package = types.ModuleType("ourd.egcf.algebra")
algebra_package.__path__ = [str(Path(egcf_package.__path__[0]) / "algebra")]
sys.modules["ourd.egcf.algebra"] = algebra_package

for qualified_name, relative_path in (
    ("ourd.errors", "errors.py"),
    ("ourd.constants", "constants.py"),
    ("ourd.reasoning.models", "reasoning/models.py"),
    ("ourd.models", "models.py"),
    ("ourd.oiec", "oiec.py"),
    ("ourd.workspace", "workspace.py"),
    ("ourd.authority", "authority.py"),
    ("ourd.persistence", "persistence.py"),
    ("ourd.hypotheses", "hypotheses.py"),
    ("ourd.reasoning.hypotheses", "reasoning/hypotheses.py"),
    ("ourd.reasoning.topology", "reasoning/topology.py"),
    ("ourd.reasoning.budget", "reasoning/budget.py"),
    ("ourd.reasoning.scoring", "reasoning/scoring.py"),
    ("ourd.reasoning.diversity", "reasoning/diversity.py"),
    ("ourd.reasoning.contradictions", "reasoning/contradictions.py"),
    ("ourd.reasoning.ablation", "reasoning/ablation.py"),
    ("ourd.reasoning.adapters", "reasoning/adapters.py"),
    ("ourd.reasoning.causal", "reasoning/causal.py"),
    ("ourd.reasoning.context", "reasoning/context.py"),
    ("ourd.reasoning.generator", "reasoning/generator.py"),
    ("ourd.reasoning.verifier", "reasoning/verifier.py"),
    ("ourd.reasoning.falsifier", "reasoning/falsifier.py"),
    ("ourd.reasoning.synthesis", "reasoning/synthesis.py"),
    ("ourd.reasoning.search", "reasoning/search.py"),
    ("ourd.reasoning.kernel", "reasoning/kernel.py"),
    ("ourd.egcf.errors", "egcf/errors.py"),
    ("ourd.egcf.ids", "egcf/ids.py"),
    ("ourd.egcf.models", "egcf/models.py"),
    ("ourd.egcf.catalog", "egcf/catalog.py"),
    ("ourd.egcf.algebra.primitives", "egcf/algebra/primitives.py"),
    ("ourd.egcf.algebra.models", "egcf/algebra/models.py"),
    ("ourd.egcf.algebra.graph", "egcf/algebra/graph.py"),
    ("ourd.egcf.algebra.ir", "egcf/algebra/ir.py"),
):
    path = Path(package.__path__[0]) / relative_path
    child_spec = importlib.util.spec_from_file_location(qualified_name, path)
    child = importlib.util.module_from_spec(child_spec)
    sys.modules[qualified_name] = child
    assert child_spec.loader is not None
    child_spec.loader.exec_module(child)

Workspace = sys.modules["ourd.workspace"].Workspace
read_only_authority = sys.modules["ourd.authority"].read_only_authority
persistence = sys.modules["ourd.persistence"]
hypotheses = sys.modules["ourd.hypotheses"]
models = sys.modules["ourd.models"]
reasoning_hypotheses = sys.modules["ourd.reasoning.hypotheses"]
reasoning_topology = sys.modules["ourd.reasoning.topology"]
reasoning_budget = sys.modules["ourd.reasoning.budget"]
reasoning_scoring = sys.modules["ourd.reasoning.scoring"]
reasoning_diversity = sys.modules["ourd.reasoning.diversity"]
reasoning_contradictions = sys.modules["ourd.reasoning.contradictions"]
reasoning_verifier_module = sys.modules["ourd.reasoning.verifier"]
reasoning_falsifier_module = sys.modules["ourd.reasoning.falsifier"]
reasoning_ablation = sys.modules["ourd.reasoning.ablation"]
reasoning_adapters = sys.modules["ourd.reasoning.adapters"]
reasoning_causal = sys.modules["ourd.reasoning.causal"]
reasoning_context = sys.modules["ourd.reasoning.context"]
reasoning_generator = sys.modules["ourd.reasoning.generator"]
reasoning_synthesis = sys.modules["ourd.reasoning.synthesis"]
reasoning_search = sys.modules["ourd.reasoning.search"]
reasoning_kernel = sys.modules["ourd.reasoning.kernel"]
reasoning_models = sys.modules["ourd.reasoning.models"]
saa_ir = sys.modules["ourd.egcf.algebra.ir"]
egcf_models = sys.modules["ourd.egcf.models"]
egcf_catalog = sys.modules["ourd.egcf.catalog"]

payloads = [
    {},
    {"b": 2, "a": 1},
    {"message": "München — 日本語", "enabled": True, "missing": None},
    {"nested": [{"z": 3, "a": 1}, [3, 2, 1]], "count": 4},
]
canonical_cases = [
    {
        "payload": payload,
        "canonical": module.canonical_json(payload),
        "sha256": module.sha256_json(payload),
    }
    for payload in payloads
]

typed_inputs = [
    ("intent", {"actor": "user", "objective": "inspect"}),
    (" Intent_Record ", {"raw_request": "inspect", "assumptions": []}),
    ("reasoning_certificate", {"problem_id": "problem", "score_bp": 7500}),
]
typed_cases = [
    {
        "object_type": object_type,
        "payload": payload,
        "typed_id": module.typed_id(object_type, payload),
    }
    for object_type, payload in typed_inputs
]

with tempfile.TemporaryDirectory() as temporary_directory:
    temporary_path = Path(temporary_directory)
    root = temporary_path / "repo"
    root.mkdir()
    (root / "README.md").write_text("StateWright\n", encoding="utf-8")
    (root / "src").mkdir()
    main = root / "src/main.cpp"
    main.write_text("int main() { return 0; }\n", encoding="utf-8")
    main.chmod(0o755)
    (root / "build").mkdir()
    (root / "build/ignored.txt").write_text("ignored\n", encoding="utf-8")
    (root / ".ourd-agent").mkdir()
    (root / ".ourd-agent/state.json").write_text("{}\n", encoding="utf-8")
    workspace = Workspace(root)
    workspace_case = {
        "snapshot": workspace.snapshot(),
        "snapshot_hash": workspace.snapshot_hash(),
    }
    authority = read_only_authority(workspace)
    authority_case = {
        "manifest": vars(authority),
        "authority_hash": authority.authority_hash,
    }

    event_path = temporary_path / "events.jsonl"
    persistence.uuid.uuid4 = lambda: persistence.uuid.UUID(
        "12345678-1234-4234-9234-123456789abc"
    )
    persistence.utc_now = lambda: "2026-09-02T00:00:00Z"
    input_payload = {
        "max_tokens": 12000,
        "access_token": "secret-value",
        "safe": "OPENAI_API_KEY=another-secret Bearer abcdef123456",
    }
    event = persistence.EventStore(event_path).append(
        "test",
        input_payload,
        run_id="run-1",
        action_id="action-1",
        transaction_id="tx-1",
    )
    event_case = {"input_payload": input_payload, "event": event}

hypothesis_proposal = {
    "proposition": "Parser   precedence is wrong",
    "model_prior_bp": 3_000,
    "assumptions": (" deterministic parser ", "deterministic parser"),
    "predictions": ("reproduction fails",),
    "falsifiers": ("clean parse succeeds",),
}
hypothesis = hypotheses.make_hypothesis(**hypothesis_proposal)
initial_state, added_hypothesis_ids = hypotheses.bounded_hypothesis_set(
    None,
    (
        hypothesis_proposal,
        {**hypothesis_proposal, "proposition": "Parser precedence is wrong", "model_prior_bp": 9_000},
    ),
    max_hypotheses=2,
)
artifact = models.EvidenceArtifact(
    artifact_id="e1",
    kind="test",
    description="grounded observation",
    sha256="a" * 64,
    source_snapshot_hash="snapshot",
    success=True,
    requirement_ids=["REQ-2", "REQ-1", "REQ-2"],
    quality_bp=7_000,
)
supported_state, changed = hypotheses.link_hypothesis_evidence(
    initial_state,
    {"e1": artifact},
    hypothesis_id=hypothesis.hypothesis_id,
    evidence_id="e1",
    relation="supports",
)
assert changed
from dataclasses import asdict
operational_hypothesis_case = {
    "hypothesis": asdict(hypothesis),
    "initial_state": asdict(initial_state),
    "added_hypothesis_ids": list(added_hypothesis_ids),
    "evidence_fingerprint": hypotheses.evidence_fingerprint(artifact),
    "supported_state": asdict(supported_state),
    "public_projection": hypotheses.public_hypothesis_projection(supported_state),
}

reasoning_initial_state = reasoning_hypotheses.build_hypothesis_set(
    (
        {"hypothesis_id": "a", "proposition": "A", "prior_bp": 6_000, "posterior_bp": 6_000},
        {"hypothesis_id": "b", "proposition": "B", "prior_bp": 4_000, "posterior_bp": 4_000},
    ),
    problem_id="problem",
    max_hypotheses=2,
    mutually_exclusive=True,
)
reasoning_update_base = reasoning_hypotheses.build_hypothesis_set(
    (reasoning_models.Hypothesis("a", "A", prior_bp=5_000, posterior_bp=5_000),),
    problem_id="problem",
    max_hypotheses=1,
)
reasoning_updated_state, reasoning_update_records = reasoning_hypotheses.update_hypothesis_state(
    reasoning_update_base,
    likelihoods={"a": (8_000, 2_000)},
    evidence_ids=("e1",),
)
reasoning_hypothesis_case = {
    "initial_state": asdict(reasoning_initial_state),
    "updated_state": asdict(reasoning_updated_state),
    "update_record": asdict(reasoning_update_records[0]),
}

topology_nodes = (
    reasoning_topology.make_reasoning_node(
        "premise:p",
        "premise",
        "Validated input",
        validated=True,
    ),
    reasoning_topology.make_reasoning_node(
        "conclusion:p",
        "conclusion",
        "Grounded result",
        confidence_bp=8_000,
        path_id="p",
        material=True,
    ),
)
topology_edges = (
    reasoning_topology.make_reasoning_edge(
        "premise:p",
        "conclusion:p",
        "entails",
        "formal",
    ),
)
reasoning_topology_value = reasoning_models.ReasoningTopology(
    problem_id="problem",
    nodes=tuple(reversed(topology_nodes)),
    edges=topology_edges,
)
reasoning_topology_case = {
    "nodes": [asdict(item) for item in topology_nodes],
    "edge": asdict(topology_edges[0]),
    "inference_id": reasoning_topology.inference_identity(
        "premise:p",
        "conclusion:p",
        "entails",
        "deductive",
    ),
    "payload": reasoning_topology.reasoning_topology_payload(reasoning_topology_value),
    "signature": reasoning_models.stable_hash(
        reasoning_topology.reasoning_topology_payload(reasoning_topology_value)
    ),
}

reasoning_step = reasoning_models.ReasoningStep(
    step_id="p1:step",
    claim="Evidence supports the bounded action",
    premises=("problem", "h1"),
    evidence_ids=("e1",),
    inference="deductive",
    confidence_bp=9_000,
    assumptions=("Bounded input",),
    falsifier="A counterexample defeats the claim.",
)
reasoning_path = reasoning_models.ReasoningPath(
    path_id="p1",
    perspective="direct",
    hypothesis_ids=("h1",),
    steps=(reasoning_step,),
    conclusion="Evidence supports bounded action",
    estimated_cost_bp=500,
    goal_relevance_bp=9_000,
)
reasoning_alternative = reasoning_models.ReasoningPath(
    path_id="p2",
    perspective="causal",
    hypothesis_ids=("h1",),
    steps=(
        reasoning_models.ReasoningStep(
            step_id="p2:step",
            claim="A causal account supports the action",
            premises=("problem", "h1"),
            evidence_ids=("e1",),
            inference="causal",
            confidence_bp=8_500,
            falsifier="Reverse causality defeats the claim.",
        ),
    ),
    conclusion="Bounded action follows from the causal account",
    estimated_cost_bp=750,
    goal_relevance_bp=8_500,
)
diverse_paths = reasoning_diversity.bind_diversity_scores(
    (reasoning_path, reasoning_alternative)
)
reasoning_verifier = reasoning_models.VerifierReport(
    report_id="verifier:p1",
    path_id="p1",
    step_scores=(("p1:step", 9_000),),
    premise_validity_bp=9_000,
    evidence_support_bp=9_000,
    inference_quality_bp=9_000,
    consistency_bp=9_500,
    completeness_bp=8_500,
    weakest_step_bp=9_000,
    score_bp=9_000,
    verdict="ACCEPT",
)
reasoning_falsifier = reasoning_models.FalsifierReport(
    report_id="falsifier:p1",
    path_id="p1",
    searched_falsifiers=("counterexample",),
    alternative_explanations=("Alternative cause",),
    severity_bp=6_500,
    survival_bp=8_000,
    residual_uncertainty_bp=1_000,
    verdict="SURVIVES",
)
reasoning_metrics = reasoning_scoring.score_reasoning_path(
    path=diverse_paths[0],
    verifier=reasoning_verifier,
    falsifier=reasoning_falsifier,
    declared_evidence_ids=("e1",),
)
reasoning_candidates = reasoning_models.CandidateSet(
    problem_id="problem",
    paths=diverse_paths,
    verifier_reports=(reasoning_verifier,),
    falsifier_reports=(reasoning_falsifier,),
    metrics=(reasoning_metrics,),
    selected_path_id="p1",
)
contradiction_candidates = reasoning_models.CandidateSet(
    paths=(reasoning_path,),
    verifier_reports=(
        reasoning_models.VerifierReport(
            **{
                **asdict(reasoning_verifier),
                "contradictions": ("logical conflict",),
            }
        ),
    ),
    falsifier_reports=(reasoning_falsifier,),
    metrics=(reasoning_metrics,),
    selected_path_id="p1",
)
contradictions = reasoning_contradictions.build_contradiction_records(
    contradiction_candidates
)
resolved_contradiction = reasoning_contradictions.resolve_contradiction(
    contradictions[0],
    resolution_evidence_ids=("e2",),
)
dimension_budget = models.DimensionBudget(
    max_active_relations=80,
    max_active_hypotheses=8,
    max_candidate_actions=6,
    max_decomposition_depth=5,
    max_branch_factor=4,
)
derived_budget = reasoning_budget.derive_reasoning_budget(
    dimension_budget=dimension_budget,
    uncertainty_bp=6_000,
    difficulty_bp=4_000,
    verifier_disagreement_bp=2_500,
    configured_max_candidates=6,
    configured_max_provider_calls=16,
)
reasoning_evaluation_case = {
    "step": asdict(reasoning_step),
    "path": asdict(reasoning_path),
    "score_configuration": asdict(reasoning_models.ScoreConfiguration()),
    "diversity_configuration": asdict(reasoning_models.DiversityConfiguration()),
    "structure_material": reasoning_diversity.path_structure_material(reasoning_path),
    "structure_signature": reasoning_diversity.path_structure_signature(reasoning_path),
    "diverse_paths": [asdict(item) for item in diverse_paths],
    "metrics": asdict(reasoning_metrics),
    "agreement_bp": reasoning_scoring.conclusion_agreement_bp(reasoning_candidates),
    "confidence_bp": reasoning_scoring.derive_reasoning_confidence_bp(reasoning_candidates),
    "contradictions": [asdict(item) for item in contradictions],
    "resolved_contradiction": asdict(resolved_contradiction),
    "derived_budget": asdict(derived_budget),
    "voi_bp": reasoning_budget.expected_value_of_information_bp(
        expected_quality_gain_bp=2_000,
        cost_bp=750,
    ),
}

reasoning_oracle_hypothesis = reasoning_models.Hypothesis(
    hypothesis_id="h1",
    proposition="The bounded action is supported",
    prior_bp=5_000,
    posterior_bp=7_500,
    supporting_evidence=("e1",),
    assumptions=("Bounded input",),
)
all_process_checks = {
    name: True for name in reasoning_verifier_module.PROCESS_CHECKS
}
verifier_payload = {
    "steps": [
        {
            "step_id": "p1:step",
            "checks": all_process_checks,
            "failures": [],
        }
    ],
    "contradictions": [],
    "missing_assumptions": [],
}
verified_report = reasoning_verifier_module.verify_reasoning_path(
    path=reasoning_path,
    hypotheses=(reasoning_oracle_hypothesis,),
    declared_evidence_ids=("e1",),
    payload=verifier_payload,
)
alternative_report = reasoning_falsifier_module.falsify_reasoning_path(
    path=reasoning_path,
    payload={
        "searched_falsifiers": ["alternative"],
        "alternative_explanations": ["A second mechanism fits."],
        "survival_bp": 9_000,
    },
)
future_defeat_report = reasoning_falsifier_module.falsify_reasoning_path(
    path=reasoning_path,
    payload={
        "searched_falsifiers": ["possible future confound"],
        "unresolved_defeat_conditions": ["A future confound may be discovered."],
        "evidence_reversal_conditions": [],
        "survival_bp": 9_000,
    },
    declared_evidence_ids=("e1",),
)
reasoning_validation_case = {
    "process_checks": list(reasoning_verifier_module.PROCESS_CHECKS),
    "verified_report": asdict(verified_report),
    "alternative_report": asdict(alternative_report),
    "future_defeat_report": asdict(future_defeat_report),
}

certificate_kernel = reasoning_kernel.SuperReasoningKernel(max_candidates=4)
certificate_problem = certificate_kernel.create_problem(
    statement="A bounded claim is supported by e1.",
    goal="Return the supported conclusion.",
    source_snapshot_hash="snapshot",
    boundary_signature="boundary",
    dimension_signature="dimension",
    evidence_ids=("e1",),
    uncertainty_bp=2_000,
    difficulty_bp=1_000,
)
certificate_hypothesis = reasoning_hypotheses.build_hypothesis_set(
    (
        {
            "hypothesis_id": "h1",
            "proposition": "The bounded claim is supported",
            "prior_bp": 5_000,
            "posterior_bp": 8_000,
            "supporting_evidence": ("e1",),
        },
    ),
    problem_id=certificate_problem.problem_id,
    max_hypotheses=1,
).hypotheses[0]
certificate_step = reasoning_models.ReasoningStep(
    step_id="winner:step",
    claim="e1 supports the bounded claim",
    premises=("problem", "h1"),
    evidence_ids=("e1",),
    inference="deductive",
    confidence_bp=9_000,
    falsifier="A conflicting declared observation defeats the claim.",
)
certificate_path = reasoning_models.ReasoningPath(
    path_id="winner",
    perspective="direct",
    hypothesis_ids=("h1",),
    steps=(certificate_step,),
    conclusion="The bounded claim is supported.",
    estimated_cost_bp=500,
    goal_relevance_bp=9_000,
)
certificate_path = __import__("dataclasses").replace(
    certificate_path,
    structure_signature=reasoning_diversity.path_structure_signature(certificate_path),
)
certificate_checks = {
    name: True for name in reasoning_verifier_module.PROCESS_CHECKS
}
certificate_verifier = reasoning_verifier_module.verify_reasoning_path(
    path=certificate_path,
    hypotheses=(certificate_hypothesis,),
    declared_evidence_ids=("e1",),
    payload={
        "steps": [
            {
                "step_id": "winner:step",
                "checks": certificate_checks,
                "failures": [],
            }
        ],
        "contradictions": [],
        "missing_assumptions": [],
    },
)
certificate_falsifier = reasoning_falsifier_module.falsify_reasoning_path(
    path=certificate_path,
    payload={"searched_falsifiers": ["declared conflict"], "survival_bp": 9_000},
    declared_evidence_ids=("e1",),
)
certificate_metrics = reasoning_scoring.score_reasoning_path(
    path=certificate_path,
    verifier=certificate_verifier,
    falsifier=certificate_falsifier,
    declared_evidence_ids=("e1",),
)
certificate_synthesis = reasoning_synthesis.fallback_to_verified_winner(
    winner=certificate_path,
    verifier=certificate_verifier,
)
certificate_candidates = reasoning_models.CandidateSet(
    problem_id=certificate_problem.problem_id,
    paths=(certificate_path,),
    verifier_reports=(certificate_verifier,),
    falsifier_reports=(certificate_falsifier,),
    metrics=(certificate_metrics,),
    selected_path_id="winner",
    surviving_path_ids=("winner",),
    synthesis=certificate_synthesis,
    score_config_id=reasoning_scoring.DEFAULT_SCORE_CONFIGURATION.config_id,
    score_config_hash=reasoning_scoring.DEFAULT_SCORE_CONFIGURATION.signature,
    diversity_config_hash=reasoning_models.stable_hash(
        {
            "configuration": reasoning_diversity.DEFAULT_DIVERSITY_CONFIGURATION.signature,
            "filter_enabled": True,
        }
    ),
    ablation_id=certificate_kernel.ablation.ablation_id,
    ablation_config_hash=certificate_kernel.ablation.signature,
)
candidate_material = asdict(certificate_candidates)
candidate_material.pop("schema_version", None)
candidate_material.pop("signature", None)
certificate_candidates = __import__("dataclasses").replace(
    certificate_candidates,
    signature=reasoning_models.stable_hash(candidate_material),
)
certificate_topology_nodes = (
    reasoning_topology.make_reasoning_node(
        "premise:certificate", "premise", certificate_problem.statement, validated=True
    ),
    reasoning_topology.make_reasoning_node(
        "conclusion:winner",
        "conclusion",
        certificate_path.conclusion,
        path_id="winner",
        material=True,
    ),
)
certificate_topology_edges = (
    reasoning_topology.make_reasoning_edge(
        "premise:certificate", "conclusion:winner", "entails", "deductive"
    ),
)
certificate_topology = reasoning_models.ReasoningTopology(
    problem_id=certificate_problem.problem_id,
    nodes=certificate_topology_nodes,
    edges=certificate_topology_edges,
)
certificate_topology = __import__("dataclasses").replace(
    certificate_topology,
    signature=reasoning_models.stable_hash(
        reasoning_topology.reasoning_topology_payload(certificate_topology)
    ),
)
certificate_budget = reasoning_budget.derive_reasoning_budget(
    dimension_budget=models.DimensionBudget(max_candidate_actions=4),
    uncertainty_bp=certificate_problem.uncertainty_bp,
    difficulty_bp=certificate_problem.difficulty_bp,
    configured_max_candidates=4,
    configured_max_provider_calls=16,
)
assembled_certificate_topology = reasoning_topology.build_reasoning_topology(
    problem=certificate_problem,
    hypotheses=(certificate_hypothesis,),
    candidates=certificate_candidates,
)
reasoning_certificate = certificate_kernel.certify(
    problem=certificate_problem,
    hypotheses=(certificate_hypothesis,),
    budget=certificate_budget,
    candidates=certificate_candidates,
    topology=certificate_topology,
)
operation_choice = reasoning_context.choose_reasoning_operation(
    budget=certificate_budget,
    expected_gains_bp={"VERIFY_AGAIN": 2_000, "REFINE_DIMENSION": 2_000},
)
reasoning_certification_case = {
    "problem": asdict(certificate_problem),
    "ablation": asdict(certificate_kernel.ablation),
    "operation_choice": asdict(operation_choice),
    "synthesis": asdict(certificate_synthesis),
    "candidate_set": asdict(certificate_candidates),
    "topology": asdict(certificate_topology),
    "assembled_topology": asdict(assembled_certificate_topology),
    "budget": asdict(certificate_budget),
    "hypothesis": asdict(certificate_hypothesis),
    "certificate": asdict(reasoning_certificate),
}

synthesis_payload = {
    "conclusion": "The bounded claim is supported.",
    "source_path_ids": ["winner"],
    "accepted_step_ids": ["winner:step"],
    "rejected_step_ids": [],
    "remaining_uncertainties": ["Residual uncertainty"],
    "confidence_bp": 8_500,
}
validated_synthesis_path = reasoning_synthesis._make_synthesis_path(
    problem=certificate_problem,
    winner=certificate_path,
    sources=(certificate_path,),
    conclusion=synthesis_payload["conclusion"],
    accepted_step_ids=synthesis_payload["accepted_step_ids"],
)
validated_synthesis_result = reasoning_models.SynthesisResult(
    winning_path_id=certificate_path.path_id,
    synthesized_path_id=validated_synthesis_path.path_id,
    source_path_ids=(certificate_path.path_id,),
    accepted_node_ids=tuple(step.step_id for step in validated_synthesis_path.steps),
    rejected_node_ids=(),
    merged_conclusion=synthesis_payload["conclusion"],
    remaining_uncertainties=("Residual uncertainty",),
    confidence_bp=min(certificate_verifier.score_bp, 8_500),
    verifier_report_id="",
    topology_signature=validated_synthesis_path.structure_signature,
    verified=False,
    failure_reasons=(
        "synthesis verification disabled by qualification ablation",
    ),
)
reasoning_synthesis_case = {
    "payload": synthesis_payload,
    "path": asdict(validated_synthesis_path),
    "result": asdict(validated_synthesis_result),
}

context_hypothesis = reasoning_hypotheses.build_hypothesis_set(
    (
        {
            "hypothesis_id": "h-context",
            "proposition": "Calibration should be confirmed",
            "prior_bp": 5_000,
            "posterior_bp": 5_000,
            "assumptions": ("  confirm   calibration  ",),
        },
    ),
    problem_id="context",
    max_hypotheses=1,
).hypotheses[0]
context_budget = reasoning_models.ReasoningBudget(
    maximum_candidates=1,
    candidate_count=1,
    verifier_count=1,
    falsifier_count=0,
    max_context_items=4,
    operation_costs_bp=(("REFINE_DIMENSION", 0),),
)
projected_context = reasoning_context.project_reasoning_context(
    problem=certificate_problem,
    hypotheses=(context_hypothesis,),
    collision_ids=("collision:z", "collision:a"),
    top_evidence_ids=("e2", "e1", "e2"),
    budget=context_budget,
)
context_operation = reasoning_context.choose_reasoning_operation(
    budget=context_budget,
    expected_gains_bp={"REFINE_DIMENSION": 1_000},
)
reasoning_context_case = {
    "hypothesis": asdict(context_hypothesis),
    "context": asdict(projected_context),
    "operation": asdict(context_operation),
}

generator_payload = {
    "conclusion": "The bounded claim is supported.",
    "hypothesis_ids": ["h1"],
    "provider_confidence_bp": 8_500,
    "estimated_cost_bp": 500,
    "goal_relevance_bp": 9_000,
    "risk_bp": 1_000,
    "steps": [
        {
            "step_id": "candidate:step",
            "claim": "e1 supports the bounded claim",
            "premises": ["problem.statement", "h1"],
            "evidence_ids": ["e1"],
            "inference": "deductive",
            "confidence_bp": 9_000,
            "assumptions": [],
            "falsifier": "A conflicting declared observation defeats the claim.",
        }
    ],
}
reasoning_generator_case = {
    "perspective_names": list(reasoning_generator.perspective_names(10)),
    "direct_contract": reasoning_generator.perspective_contract("direct"),
    "independent_probe_contract": reasoning_generator.perspective_contract(
        "independent_probe_09"
    ),
    "problem_context": reasoning_generator.provider_problem_context(
        certificate_problem
    ),
    "hypothesis_context": reasoning_generator.provider_hypothesis_context(
        certificate_hypothesis
    ),
    "batch_tool": reasoning_generator.reasoning_batch_tool("candidates"),
    "object_tool": reasoning_generator.reasoning_object_tool(
        ("conclusion", "steps"), required_keys=("conclusion",)
    ),
    "parsed_path": asdict(
        reasoning_generator.parse_reasoning_path(
            payload=generator_payload,
            problem=certificate_problem,
            hypotheses=(certificate_hypothesis,),
            perspective="direct",
            budget=certificate_budget,
        )
    ),
}

standard_ablations = reasoning_ablation.standard_ablation_configurations()
reasoning_search_case = {
    "rejected_falsifier": asdict(
        reasoning_search._rejected_falsifier(certificate_path)
    ),
    "bypass_verifier": asdict(
        reasoning_search._bypass_verifier(certificate_path, ("e1",))
    ),
    "bypass_falsifier": asdict(
        reasoning_search._bypass_falsifier(certificate_path)
    ),
    "standard_ablations": [asdict(item) for item in standard_ablations],
    "ablation_pipelines": [
        reasoning_ablation.ablation_pipeline(item) for item in standard_ablations
    ],
    "assembled_candidate_set": asdict(certificate_candidates),
}

saa_binary_algorithm = {
    "name": "display-only-name",
    "inputs": [
        {"position": 0, "name": "left"},
        {"position": 1, "name": "right"},
    ],
    "outputs": [
        {"position": 0, "name": "answer", "source": {"node": "combine"}},
    ],
    "nodes": [
        {
            "id": "combine",
            "primitive": "ADD",
            "operands": [{"input": 0}, {"input": 1}],
            "attributes": {
                "display_name": "ignored source label",
                "description": "ignored documentation",
            },
        }
    ],
    "entry_nodes": ["combine"],
}
saa_ir_case = {
    "add": saa_ir.canonicalize_mapping(saa_binary_algorithm).to_dict(),
}

egcf_catalog_data = egcf_catalog.command_catalog()
egcf_contract = egcf_catalog.command_contract("algorithm", "search")
egcf_settings = {
    **egcf_catalog_data["defaults"]["algorithm"],
    **egcf_catalog_data["overrides"].get("algorithm.search", {}),
}
egcf_definition = egcf_models.CommandDefinition(
    namespace="algorithm",
    name="search",
    version=1,
    intent_kinds=["algorithm.search", "algorithm"],
    input_schema=egcf_contract["input_schema"],
    output_schema=egcf_contract["output_schema"],
    preconditions=egcf_contract["preconditions"],
    postconditions=egcf_contract["postconditions"],
    invariants=egcf_contract["invariants"],
    evidence_requirements=egcf_contract["evidence_requirements"],
    capability_query={"level": egcf_settings["level"], "facets": egcf_settings["facets"]},
    algorithm_query={"command_id": "algorithm.search@1"},
    risk_policy=egcf_settings["risk"],
    rollback_policy=egcf_settings["rollback"],
    budget_policy={"actions": 1, "retries": 0},
    approval_policy=egcf_settings["approval"],
    lifecycle_policy={"compressible": egcf_settings["level"] in {"C0", "C1"}},
    description="EGCF semantic command algorithm.search",
    aliases=[
        alias
        for alias, target in egcf_catalog_data["aliases"].items()
        if target == "algorithm.search@1"
    ],
)
egcf_description = {
    "object_id": egcf_definition.object_id,
    **egcf_definition.to_dict(),
    "command_id": egcf_definition.command_id,
}
egcf_command_case = {
    "definition": egcf_definition.to_dict(),
    "object_id": egcf_definition.object_id,
    "description": egcf_description,
}

reasoning_adapter_case = {
    "arithmetic": asdict(
        reasoning_adapters.evaluate_decimal_expression("2 + 3 * 4")
    ),
    "predicate": asdict(
        reasoning_adapters.evaluate_decimal_expression(
            "x > 1 and x < 5", variables={"x": "3"}
        )
    ),
    "symbolic": asdict(
        reasoning_adapters.symbolic_equivalence(
            "(x + 1)^2", "x*x + 2*x + 1"
        )
    ),
    "residual": asdict(
        reasoning_adapters.numerical_residual_check(
            "x*x", "x + 1", points=({"x": "2"},)
        )
    ),
    "dimensional": asdict(
        reasoning_adapters.dimensional_equivalence(
            {"L": 1, "T": -2, "unused": 0},
            {"L": 1, "T": -2},
            equation="force per mass",
        )
    ),
    "finite_domain": asdict(
        reasoning_adapters.finite_domain_check(
            "x + y < 3", domains={"x": ("0", "1"), "y": ("0", "1")}
        )
    ),
}

causal_edge = reasoning_causal.CausalEdge(
    edge_id="edge:temperature-yield",
    source_id="temperature",
    target_id="yield",
    relation="causes",
    evidence_ids=("experiment:1",),
    temporal_ordered=True,
)
causal_intervention = reasoning_causal.Intervention(
    intervention_id="intervention:temperature",
    variable_id="temperature",
    assigned_value="350 K",
)
causal_assessment = reasoning_causal.assess_causal_claim(
    claim="temperature increases yield",
    source_id="temperature",
    target_id="yield",
    edges=(causal_edge,),
    intervention=causal_intervention,
    proposed_confidence_bp=8_500,
)
reasoning_causal_case = {
    "edge": asdict(causal_edge),
    "intervention": asdict(causal_intervention),
    "assessment": asdict(causal_assessment),
}

document = {
    "schema_version": 1,
    "oracle_commit": os.environ["ORACLE_COMMIT"],
    "canonical_json_cases": canonical_cases,
    "typed_id_cases": typed_cases,
    "workspace_case": workspace_case,
    "authority_case": authority_case,
    "event_case": event_case,
    "operational_hypothesis_case": operational_hypothesis_case,
    "reasoning_hypothesis_case": reasoning_hypothesis_case,
    "reasoning_topology_case": reasoning_topology_case,
    "reasoning_evaluation_case": reasoning_evaluation_case,
    "reasoning_validation_case": reasoning_validation_case,
    "reasoning_certification_case": reasoning_certification_case,
    "reasoning_synthesis_case": reasoning_synthesis_case,
    "reasoning_context_case": reasoning_context_case,
    "reasoning_generator_case": reasoning_generator_case,
    "reasoning_search_case": reasoning_search_case,
    "reasoning_adapter_case": reasoning_adapter_case,
    "reasoning_causal_case": reasoning_causal_case,
    "saa_ir_case": saa_ir_case,
    "egcf_command_case": egcf_command_case,
}
Path(os.environ["OUTPUT"]).write_text(
    json.dumps(document, indent=2, sort_keys=True, ensure_ascii=False) + "\n",
    encoding="utf-8",
)
PY

sha256sum "$output" | awk '{print $1}' >"$output.sha256"
printf 'Exported foundation fixtures from %s\n' "$oracle_commit"
