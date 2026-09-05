# Explicit scalar source translation

The internet feed accepts bounded scalar procedures inside the existing explicit
source format:

```text
Scale algorithm; inputs: x; outputs: y; procedure: return 2*x
Offset algorithm; inputs: x; outputs: y; procedure: return x - 5/4
```

These extend the existing identity and `return a*x+b` forms. Scale accepts a
nonzero signed integer or rational coefficient. Offset accepts the declared input
followed by addition or subtraction of an unsigned integer or rational constant.
Each numeric component has at most 64 digits; abbreviated expressions use base-10
numbers, including leading zeros. Denominators must be nonzero. Procedures remain
limited to 256 characters and a single declared input and output.

The translator normalizes exact rational coefficients into the existing
MULTIPLY/ADD representation, or IDENTITY when slope is one and bias is zero.
New abbreviated forms record `exact-scalar-procedure-v3` provenance. Existing
identity and full affine forms retain their v2 provenance so their immutable
translation evidence remains reproducible.

Wrong variables, extra operations, conditional prose, zero slopes, and zero
denominators remain quarantined. Source-span binding and translation revalidation
still apply. Translation only stages a candidate; qualification, promotion policy,
and probation remain required. Mathematical-context review flags still block
translation even when the fragment contains a supported example.

This extension does not translate the cryptographic RFCs from the September 5
pilot or establish accepted yield on live sources. Keep polling volume unchanged
until independently grounded protocols and a compatible real source demonstrate
the complete lifecycle. Fixtures in `Tests/egcf/test_internet_feed.cpp` cover the
new forms, rational normalization, replay, provenance tampering, and rejection
boundaries.
