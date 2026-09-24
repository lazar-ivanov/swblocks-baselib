# H19 — a plain client still reaches OpenSSL: deferral record

**Date:** 2026-09-24. **Status:** deferred by the maintainer, with the condition that reverses it
recorded. **The guard and its explanation are in place; astra's actual remedy is not.**

Written because this item was **missing from the consolidated owed list until 2026-09-24** — it was
recorded only in the verification record, which is not the place a reader is told to look for what is
left. H19 itself is discharged as *recorded and accepted*; what follows is the part that is not.

---

## 1. The finding

Astra's H19: a client that will never speak TLS still pulls OpenSSL headers into its translation
unit, because `ClientConnectionTaskBase.h` includes `<baselib/crypto/CryptoBase.h>` unconditionally.

**Astra's remedy was not the guard.** It asked for the TLS specialization to be **moved behind a
TLS-specific header**, so that a plain build does not reach OpenSSL at all.

## 2. What is in place, and it is not that

Three things landed and are correct as far as they go:

- the unconditional `#include <baselib/crypto/CryptoBase.h>` at `ClientConnectionTaskBase.h:29`;
- an `OPENSSL_VERSION_NUMBER < 0x10100000L` `#error` guard at `:48`, with a comment explaining why it
  is keyed on the **OpenSSL capability** rather than on `BL_DEVENV_VERSION` — no public header may
  require that macro;
- the consequence written into `httpclient/PreCompiled.h:38-39`: because this header reaches OpenSSL,
  it stays out of the precompiled header.

So the build **fails loudly** on an OpenSSL too old to support what the specialization needs, and the
cost is documented where a reader meets it. **What none of that does is stop a plain client reaching
OpenSSL**, which is what astra asked for.

## 3. Why it is deferred

**It buys a dependency nobody has asked to be rid of.** No consumer in this repository or its
downstreams has required an OpenSSL-free build of the HTTP client, and the `#error` guard means a
broken combination is a compile failure rather than a runtime surprise.

**And the change is not the ten lines the estimate suggests.** Moving a specialization behind a
TLS-specific header is a header-topology change: every translation unit that reaches
`ClientConnectionTaskBase.h` today is affected, the precompiled-header note at
`httpclient/PreCompiled.h` becomes wrong or unnecessary, and the split has to be drawn so that the
*plain* path compiles with the TLS header absent rather than merely unused. That is the part that
decides the work, and nobody has drawn it.

## 4. What would reverse this

**A consumer that needs a genuinely OpenSSL-free build of the HTTP client.** That is the whole
condition. Absent one, the guard plus the two comments are the honest state: the dependency exists,
it is named where it is met, and it is not paid for in behaviour.

Secondary, and weaker on its own: if `httpclient/PreCompiled.h`'s exclusion ever becomes a
measurable build-time cost, the topology change buys that back as well.

## 5. What this record does not establish

- **The size of the change.** §3's argument that it is more than ten lines is from reading the
  include graph, not from attempting it.
- **Whether any downstream already wants this.** Not surveyed — the claim "nobody has asked" is about
  this repository and its recorded findings, not about consumers nobody here has spoken to.
- **What the split would look like.** Deliberately not designed. Designing it would be most of the
  work, and the point of deferring is not to spend that until the condition in §4 is met.
