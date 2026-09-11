#!/usr/bin/env python3
"""Bounded offline dependency planning. Never opens or mutates an SAA store."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import tempfile


VERSION = "saa-foundation-planner-v1"
ROOT = Path(__file__).resolve().parents[1]
MAX_BYTES = 1024 * 1024


def read_json(path):
    with Path(path).open("rb") as stream:
        raw = stream.read(MAX_BYTES + 1)
    if len(raw) > MAX_BYTES:
        raise ValueError(f"Input exceeds {MAX_BYTES} bytes: {path}")
    return json.loads(raw), hashlib.sha256(raw).hexdigest()


def atomic_write(path, value):
    data = (json.dumps(value, indent=2, sort_keys=True) + "\n").encode("ascii")
    if len(data) > MAX_BYTES:
        raise ValueError("Output exceeds planner byte budget")
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=path.name + ".", dir=path.parent)
    try:
        with os.fdopen(fd, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def catalogue_graph(catalogue):
    if catalogue.get("schema_version") != 1:
        raise ValueError("Unsupported catalogue schema")
    operations = catalogue["operations"]
    sources = catalogue["sources"]
    if not isinstance(operations, dict) or not 1 <= len(operations) <= 128:
        raise ValueError("Catalogue must contain 1..128 operations")
    if not isinstance(sources, dict) or len(sources) > 128:
        raise ValueError("Source catalogue exceeds limit")
    for name, operation in operations.items():
        dependencies = operation["dependencies"]
        if not isinstance(dependencies, list) or len(dependencies) > 32:
            raise ValueError(f"Invalid dependency list: {name}")
        if len(set(dependencies)) != len(dependencies):
            raise ValueError(f"Duplicate dependency: {name}")
        for child in dependencies:
            if child not in operations:
                raise ValueError(f"Undeclared dependency: {name} -> {child}")
        for source in operation["sources"]:
            if source not in sources:
                raise ValueError(f"Undeclared source: {source}")
    visiting, complete = set(), set()

    def visit(name):
        if name in visiting:
            raise ValueError(f"Dependency cycle at {name}")
        if name in complete:
            return
        visiting.add(name)
        for child in operations[name]["dependencies"]:
            visit(child)
        visiting.remove(name)
        complete.add(name)

    for name in operations:
        visit(name)
    return operations


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--catalogue", type=Path,
                        default=ROOT / "resources/saa/foundations-v1.json")
    parser.add_argument("--roots", nargs="+", default=["airy_ai", "polynomial_exact_rational"])
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--max-nodes", type=int, default=8)
    parser.add_argument("--max-depth", type=int, default=8)
    args = parser.parse_args()
    if not 1 <= args.max_nodes <= 128 or not 0 <= args.max_depth <= 32:
        parser.error("Node budget must be 1..128; depth must be 0..32")
    catalogue, digest = read_json(args.catalogue)
    operations = catalogue_graph(catalogue)
    roots = sorted(set(args.roots))
    if any(root not in operations for root in roots):
        parser.error("Unknown root operation")
    contract, contract_digest = read_json(
        ROOT / "resources/saa/contracts/polynomial-exact-rational-v1.json")
    binding = {"planner": VERSION, "catalogue_sha256": digest,
               "contract_sha256": contract_digest, "roots": roots}
    completed = []
    if args.resume:
        prior, _ = read_json(args.output)
        if prior.get("binding") != binding:
            parser.error("Resume requires identical catalogue, contract, roots and planner")
        completed = prior.get("completed_operations", [])
        if not isinstance(completed, list) or any(n not in operations for n in completed):
            parser.error("Invalid resume operation identities")
    elif args.output.exists():
        parser.error("Output exists; use --resume or a new output path")

    # Reconstruct reachability from authoritative inputs, not saved graph payloads.
    queue = [(name, 0) for name in roots]
    depth_by_name = {}
    deferred = set()
    while queue:
        name, depth = queue.pop(0)
        if depth > args.max_depth:
            deferred.add(name)
            continue
        if name in depth_by_name:
            continue
        depth_by_name[name] = depth
        queue.extend((child, depth + 1) for child in operations[name]["dependencies"])
    deferred.difference_update(depth_by_name)
    if any(name not in depth_by_name for name in completed):
        parser.error("Resume depth excludes previously completed operations")
    done = set(completed)
    pending = [name for name in depth_by_name if name not in done]
    done.update(pending[:args.max_nodes])
    pending = pending[args.max_nodes:]
    nodes = [{"operation": name, "depth": depth_by_name[name], **operations[name],
              "context_status": "NOT_NATIVE_REVIEWED",
              "qualification_status": "NOT_ESTABLISHED_BY_THIS_PLANNER"}
             for name in sorted(done)]
    source_ids = sorted({source for node in nodes for source in node["sources"]})
    blockers = sorted({reason for node in nodes for reason in node["blockers"]})
    report = {
        "schema_version": 1, "binding": binding,
        "scope": "Offline planning only; no native source assessment or acceptance evidence",
        "planning_status": "BOUNDED_PENDING" if pending or deferred else "CATALOGUE_TRAVERSAL_COMPLETE",
        "mathematical_context_status": "INCOMPLETE",
        "inventory": catalogue["inventory"],
        "completed_operations": sorted(done), "pending_operations": pending,
        "depth_deferred_operations": sorted(deferred), "nodes": nodes,
        "sources": {name: catalogue["sources"][name] for name in source_ids},
        "proposed_contract": contract if "polynomial_exact_rational" in done else None,
        "blockers": sorted(set(blockers + ["SOURCE_SNAPSHOTS_AND_NATIVE_REVIEWS_REQUIRED"])),
        "acceptance_claim": "NONE",
        "next_step": "Resolve reviewed source closure and extend qualification adapter before experiments"
    }
    atomic_write(args.output, report)
    print(json.dumps({"report": str(args.output.resolve()),
                      "planning_status": report["planning_status"],
                      "completed_operations": len(done),
                      "pending_operations": len(pending),
                      "depth_deferred_operations": len(deferred),
                      "acceptance_claim": "NONE"}))


if __name__ == "__main__":
    try:
        main()
    except (ValueError, KeyError, TypeError, OSError) as error:
        raise SystemExit(f"Foundation planning failed: {error}")
