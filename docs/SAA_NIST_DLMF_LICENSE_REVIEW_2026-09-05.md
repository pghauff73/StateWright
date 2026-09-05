# NIST DLMF license review

Reviewed: 2026-09-05 (Australia/Brisbane).

Decision: **Not approved for the current automated ingestion workflow.**
The 20 `nist-dlmf` watches retain `review-required` status; this review does
not enable them or change the source-group registry.

## Evidence and scope

Authoritative source: [DLMF Notices](https://dlmf.nist.gov/about/notices),
Copyright section, checked during this review. The registry currently points
to `/about/copyright`; use the Notices page as the review evidence instead.

NIST states that DLMF authors assigned copyright to it. The notice allows
limited copying and internal distribution for research and teaching, but
prohibits commercial-purpose copying and bulk copying or redistribution.
Government hosting therefore does not establish public-domain permission
for this collection.

The proposed lane contains 20 section URLs with weekly polling defaults.
The native feeder acquires stored snapshots and extracts persistent fragments
for SAA processing. This is not merely a list of links. The notice does not
clearly authorize that intended repeated acquisition and retention. This is
an operational approval decision, not a determination that every individual
research use is unlawful.

## Evidence needed before approval

1. Describe the intended use: research or commercial context, exact pages,
   polling frequency, retained snapshots and fragments, extraction and
   transformation, and any redistribution of source material or outputs.
2. Request written permission or clarification through the copyright-policy
   contact on the official Notices page. Ask explicitly whether that scope
   is permitted, including any attribution, retention, or volume limits.
3. Retain the permission, date, reviewer, scope, and applicable conditions in
   the review record. Obtain qualified legal review if ambiguity remains.
4. Implement any required restrictions before marking only the covered scope
   verified. Regenerate the manifest/preflight and register the reviewed watch
   versions through the normal workflow; do not alter historical receipts or
   bypass the license gate.

Until then, retain the URLs as disabled references. No publisher was contacted
and no legal permission was granted by this review.
