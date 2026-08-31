# Semantic entity and event mappings

`mappings.yml` is the curated bridge from technical ROM selectors to names
used by the deassembly and native parity work. Numeric IDs are part of the
original ABI and must remain recorded even after a semantic name is proven.

The inventory command is deliberately advisory:

```bash
genie symbols type-worklist
genie symbols type-worklist --json
```

Do not infer an entity from a type number alone. Add a mapping only after
template, VM stream, handler, static call-graph, runtime trace, or parity
evidence establishes the identity.

Example entry:

```yaml
mappings:
  - name: GUARD
    scope: actor
    symbol_addresses: [0x001B7C00]
    technical_types: [0x2D]
    confidence: trace_validated
    evidence: [guard-template-trace-v1]
    description: Actor template and collision/animation consumers identify the guard entity.
```

Scopes are `actor`, `event`, `resource`, `role`, and `technical`. Use
`role` when behavior is understood but the in-game entity is not; use
`technical` when the numeric selector is the only established identity.
