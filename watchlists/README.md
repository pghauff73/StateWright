# Current SAA watchlists

Use `saa-all-lanes-100-v2.json` for the current 100-target configuration.
It replaces the 20 disabled NIST DLMF targets with 10 Fungrim and 10 Boost.Math
files, each pinned to an immutable revision and reviewed content hash.
The other 80 targets are unchanged. `saa-all-lanes-100-v1.json` is retained only
as historical evidence for its earlier feed; it is not the current configuration.

`saa-mathematics-20-v1.json` contains only the new mathematical targets. Use this
subset for a replacement-lane pilot rather than resubmitting the existing 80
watches to the one-time feeder. A manifest's requested `enabled` flag does not
bypass preflight or license checks. The feeder disables its new watches on
normal completion; these files do not create a recurring poller.

Regenerate a new 100-target manifest offline:

```sh
bash Tools/generate_saa_watchlist_100.sh ./build/statewright NEW_MANIFEST.json
```

See `docs/SAA_MATHEMATICAL_SOURCES.md` for review scope, extraction guarantees,
the separate numerical verifier, and pilot evidence. Old immutable NIST
registrations and the legacy authoritative resource template are preserved;
they have not been relicensed or enabled.
