# 2026-10-31 · Claude (cloud thread) · Copy Report button
- Repos/commits: branch feat/copy-report from master
- Did: Copy Report button in the panel's result box (`report.copy_text`, `copy_report_text`): build ids,
  Blender, system, the run's settings, input counts, result rows and warnings, step times, the worker's last
  output (last run of the session, capped at 4,000 characters), report JSON; user folder names redacted.
  Panel-only settings are stored in the result's report under `addon`. Unit tests for the text and the
  redaction, a Blender smoke check, user guide "Reporting a problem", bug form field.
- Found: nothing new.
- Measured: nothing measured (no compiled code changed: lane none, CI only).
- Next: none.
