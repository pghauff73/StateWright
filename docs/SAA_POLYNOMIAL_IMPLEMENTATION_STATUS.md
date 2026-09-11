# Exact-rational polynomial implementation status

## Implemented, not yet end-to-end qualified

- Separate execution family: `internet-exact-rational-horner-v1`.
- Fixed rational coefficients, degree 0..16, input domain [-1,1].
- Exact closed Horner graph validation; no arbitrary graph execution.
- Coefficient/input integer limits and conservative pre-allocation intermediate
  limits. Resource exhaustion raises an error rather than truncating a result.
- Shared version-aware execution entry point for candidate and reviewed oracle.
- Semantic descriptions bind the complete polynomial execution contract.
- Polynomial-specific grounded protocol version and contract binding.
- Director family matching excludes legacy/CSS protocols and stale bindings.
- Legacy identity, constant and affine execution remain separate.

The director compatibility check does not certify source policy, independence,
measurements or reviews. The existing qualification validator still checks those.

## Verified before the director-matching increment

The test build succeeded. The polynomial filter passed 47 assertions in six
cases. The `*internet*` name filter passed 959 assertions in 94 cases, including
the new cases. These results establish targeted compatibility, not complete
qualification, performance improvement or independent experiment evidence.

## Remaining objective

1. Exercise the director family matching and native protocol routing.
2. Provide an equivalent independently implemented reference/baseline path with
   declared shared dependencies and bounded resource semantics.
3. Complete the source-bound translator using reviewed, immutable sections and
   fixed coefficients; do not lift quarantine based on untrusted metadata.
4. Register a separate polynomial protocol with genuinely frozen groups, source
   bindings, independent review and real measurements. No CSS protocol reuse.
5. Collect real longitudinal integrity/probation observations under policy.
6. Integrate polynomial canonical form into the nonlinear lifecycle. Currently
   probation fails with `POLYNOMIAL_NONLINEAR_CANONICAL_FORM_REQUIRED`, rather
   than misrepresenting a polynomial as an affine transfer function.
7. Reassess a higher-level candidate only when its mathematical dependencies and
   composed-error requirements are satisfied.

No source watches or policies were broadened, and no candidate was admitted by
these implementation changes. The full implementation goal remains incomplete.
