# Security Policy

## Reporting a Vulnerability

Please do not report security vulnerabilities through public GitHub issues.

Instead, use GitHub's private vulnerability reporting for this repository
(Security tab -> "Report a vulnerability"), or email the maintainer at
asynth@gmail.com with a description of the issue and steps to reproduce.

You should receive a response within a week. Please allow time for the issue
to be investigated and fixed before any public disclosure.

## Scope

Things that are in scope: memory-safety issues in the interpreter, engine, or
generated plugin code that are reachable from untrusted input; sandbox or
FFI boundary issues.

Note that TZPL is a programming platform: Tzopilotl programs, synthdefs, and
plugins are arbitrary code by design. Running untrusted `.x` files or loading
untrusted `.dylib` plugins is equivalent to running untrusted native code and
is not itself a vulnerability.
