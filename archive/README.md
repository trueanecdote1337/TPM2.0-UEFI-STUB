# Superseded experiments

`stub/` contains the original nonfunctional DXE TPM stub and SSDT. It is retained
for history and is **not** referenced by the current OVMF integration. Do not add
its INF to the firmware alongside the SMM implementation.

`tests/` contains incremental bring-up scripts from earlier stages. Their path
assumptions and expected markers describe historical snapshots. They are not
maintained entry points. Current regression commands are in `Tests/README.md`.

Raw historical logs are kept locally in ignored `artifacts/legacy-logs/`; they are
not required to build or run the firmware. Repository history may still contain
logs from the initial commit; this cleanup does not rewrite Git history.
