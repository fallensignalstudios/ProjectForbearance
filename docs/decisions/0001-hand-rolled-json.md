# 0001 — A hand-rolled strict JSON reader and writer

Status: accepted. Supersedes nothing.

## Context

Section 19.4 of the technical design requires reproducible build commands, pinned
exact dependencies with recorded licences, and a core that builds without Unreal
installed. Section 16.1 requires canonical UTF-8 JSON with bytewise-sorted keys, no
optional whitespace, normalised escaping and decimal-string 64-bit integers.
Section 16.4 requires the loader to reject unknown schema versions, malformed
fields, excess nesting, oversized arrays and payloads above 16 MiB.

A general-purpose JSON library would satisfy the parsing but not the canonical
serialisation or the input limits, and would add a dependency to fetch.

## Decision

`core/src/json.cpp` implements the reader and writer directly, in about 500 lines.
It is deliberately narrow:

- No floating point. Every numeric literal must be an integer; a fractional or
  exponent literal is an error naming its byte offset. Content authors write
  milli-units and basis points.
- Object keys live in a bytewise-sorted map, so parse order cannot influence
  anything downstream.
- Serialisation is canonical by construction.
- Nesting depth, array length, key count and payload size are enforced limits.
- Duplicate keys, comments, unescaped control characters and unpaired surrogates
  are errors.

## Consequences

The core has no third-party dependency at all, so "pin exact dependencies" is
trivially satisfied and the offline build works. The cost is that this reader is not
a general JSON library: it will reject valid JSON that the project has no business
authoring. That is the intent.

Unicode handling is specified rather than inferred: strings must be well-formed
UTF-8, identifiers are restricted to lowercase ASCII, and the codec applies no
normalisation of its own. Display text is expected in NFC and is passed through
unchanged.
