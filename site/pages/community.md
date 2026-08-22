# Community & Contributing

TZPL is free software under the
[GNU GPL v3](https://github.com/lfnoise/tzpl/blob/main/LICENSE), developed
in the open at [github.com/lfnoise/tzpl](https://github.com/lfnoise/tzpl).

## Getting help & reporting problems

- **Bug reports, feature requests, and questions** --
  [GitHub issues](https://github.com/lfnoise/tzpl/issues). Issue templates
  are provided for bug reports and feature requests; for crashes or wrong
  audio output, a minimal `.x` script that reproduces the problem is the
  single most useful thing you can attach.

## Contributing

Read [CONTRIBUTING.md](https://github.com/lfnoise/tzpl/blob/main/CONTRIBUTING.md)
first -- it covers the build, the test suites, coding conventions
(C++23, east const, real-time-safety rules), and how to structure a pull
request. In short:

- Build from the project root with `./build.sh`.
- Language tests: `cd lang/tests && bash run_tests.sh`.
- Synthdef compiler tests: `./build/synthdef-compiler/synthdef-compiler --test`.
- The language VM, engine audio thread, and generated plugins must never
  call the system allocator or block -- patches that violate real-time
  safety will be declined regardless of how nice the feature is.

Documentation contributions are as welcome as code: the guide sources live
in [`lang/docs/`](https://github.com/lfnoise/tzpl/tree/main/lang/docs) and
every page on this site has an "Edit this page on GitHub" link at the
bottom.

## Security

To report a security vulnerability, follow
[SECURITY.md](https://github.com/lfnoise/tzpl/blob/main/SECURITY.md) --
please do not open a public issue for it.
