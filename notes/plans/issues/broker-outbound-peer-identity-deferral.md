# Broker Outbound Peer Identity: Trust Assumption and Deferral Record

This document records the decision taken on the review finding M-3 (the messaging broker's
outbound acceptor registers delivery queues under a peer id which the connecting peer declares
itself), the hardening applied now, the trust assumption the deployment must satisfy while the
deferred fix is not in place, and the deferred fix itself: mutual TLS on the outbound port with
the peer id derived from the client certificate.

**Finding:** M-3 in `notes/reviews/major/update_2026/whole-library-cxx-review-fable51.md`
(High) and the decision row M-3 in
`notes/reviews/major/update_2026/whole-library-cxx-review-fable51-decisions.md`.

**Related findings:** M-4 (association messages from unauthenticated peers) and M-15
(`sourcePeerId` filled only when empty) in the same review; both were deferred with the mutual-TLS
item on 2026-09-06 and are recorded in the last section of this document.

**Prior decision this defers against:** the server role does not verify client certificates
(`verify_none`), decision B6 in `notes/plans/issues/pr-review-residual-cxx-findings-plan.md:297-320`.

---

## Decision

**Date:** 2026-09-05
**Status:** Option (b) implemented; mutual TLS on the outbound port deferred

| # | Item | Disposition |
|---|---|---|
| 1 | Registration from a different remote address no longer demotes the active delivery queues | **Fixed** (`TcpBlockTransferClient.h`, `TcpBlockServerOutgoingBackendState::registerQueue`), unit test `IO_OutgoingBackendStateRegistrationTests` |
| 2 | The outbound port is a trusted network | **Accepted risk**, recorded below |
| 3 | Bind the outbound peer id to an authenticated identity | **Deferred**: mutual TLS with the peer id derived from the client certificate |

---

## How the outbound port identifies a peer today

The broker has two ports. On the inbound port a peer pushes messages to the broker. On the
outbound port the roles flip: the peer connects, the broker side runs a blob-protocol client
(`TcpBlockTransferClientAutoPushConnection`) and pushes messages to the peer, which answers as a
blob-protocol server. During version negotiation the peer replies with its own configured peer id
(`TcpBlockTransferServer.h`, `scheduleResponseCommand`), the broker records it as the remote id
(`TcpBlockTransferClient.h`, `handleAckPacket`) and, after the first successful heartbeat,
registers the connection as a delivery queue for that id
(`TcpBlockServerOutgoingBackendState::registerQueue`).

Nothing binds that id to anything:

- the outbound acceptor (`TcpBlockServerOutgoingT`) has no authentication callback at all, and the
  broker facade passes none to the inbound acceptor either (`BrokerFacade.h`);
- the broker's authentication is per message: a token inside the broker-protocol JSON is resolved
  to a principal and stamped on the message (`BrokerBackendProcessing.h`, `authorizeProtocolMessage`);
  the peer id is never compared with the principal;
- TLS on both ports authenticates the broker to the peer only; client certificates are not
  requested (decision B6).

Peer ids are not secrets. They appear as `sourcePeerId` / `targetPeerId` in every broker-protocol
message and in debug logs of the broker, the proxy and the clients.

## What was wrong (M-3)

Any host which can reach the outbound port and knows a victim's peer id can connect with an
ordinary messaging client configured with the victim's id (the public client factory accepts any
UUID). Before this change every registration demoted **all** active queues of that id to the
unconfirmed list and made the new connection the only active delivery queue until the others
confirmed by heartbeat. Repeating the connection every few seconds kept the victim demoted, so
the attacker received nearly all of the victim's messages, including the sender's authorized
principal. Each registration also removed the victim's proxy route
(`BrokerBackendProcessing::peerConnectedNotify`).

## What option (b) changes

`registerQueue` now receives the remote address of the accepted connection (captured in
`TcpBlockServerOutgoingT::createConnection` and bound into the notify callback) and applies this
rule:

- every active queue of the peer id is still asked to heartbeat;
- active queues registered from the **same** remote address as the new registration (or when
  either address is unknown) are demoted to the unconfirmed list until they confirm, exactly as
  before, which keeps the cooperative multi-connection behaviour of a peer on one host;
- active queues registered from a **different** remote address stay active; a stale one is
  removed when its heartbeat fails and the connection unregisters.

No wire change, no client change, no configuration change.

## What option (b) does not protect against

This is a hardening, not authentication. With the outbound port reachable, a host that knows a
peer id still:

- joins the round-robin for that id and receives a share of its messages (one in *n* + 1 for a
  peer with *n* active connections);
- triggers the dissociation of the peer's proxy route on every registration (M-3 denial of
  service leg);
- can spoof `sourcePeerId` on the inbound leg (M-15 / M-4, separate decisions).

## Trust assumption recorded (accepted risk)

**The broker's outbound port must be reachable only by hosts trusted to declare their own peer
id.** In practice: the outbound port sits on the same network segment or behind the same access
controls as the peers and proxies it serves, and is not exposed to hosts that could carry a
foreign peer id. Deployments that cannot guarantee this must not rely on option (b) and need the
deferred fix.

## Deferred fix: mutual TLS on the outbound port, peer id from the client certificate

The review proposed (a): a per-peer secret minted on an inbound `Authentication` block and echoed
in the outbound version-negotiation header. That is a wire-protocol addition and forces every
client to reorder its connection setup (authenticate inbound, obtain the secret, then connect
outbound), which the C++ client factory, the proxy backend and any non-C++ implementation would
all have to adopt behind a broker rollout flag.

The chosen deferred design avoids the wire change: **require a client certificate on the
outbound port and derive (or verify) the peer id from it.**

- The peer's certificate carries its peer UUID, for example as a SAN URI (`urn:uuid:<peer id>`) or
  in the CN. The broker verifies the chain against the deployment's peer CA and, on registration,
  refuses a queue whose declared peer id does not match the certificate.
- Prerequisites: a PKI which issues per-peer certificates; a server-role verify mode in
  `CryptoBase` for the outbound acceptor only (today `verify_none`, decision B6); a broker
  configuration flag with a staged rollout (flag off: today's behaviour; peers receive
  certificates; flag on: unbound registrations refused); the proxy backend registers with its own
  certificate.
- Client changes: present a certificate on the outbound connection. No protocol change; a client
  without a certificate keeps working while the flag is off.
- With the peer id authenticated, M-4's `sourcePeerId == connection id` rule and M-15 can be
  enforced against the same identity.

## Revisit conditions

Any of the following reopens item 3 as a merge-gate item:

- a deployment exposes the outbound port to hosts that are not trusted to declare their own peer id;
- the M-4 decision requires an authorized principal for association messages (the same identity
  should back both);
- a client-certificate PKI becomes available for the peers.

## Verification of the applied change

- `IO_OutgoingBackendStateRegistrationTests` (`utf_baselib_io`): with injected addresses, a
  registration from another address keeps the existing queue active and probes it; a registration
  from the same address and one with an unknown address demote as before; confirm, unregister and
  the double-registration error paths. The case fails with the previous rule (the second
  registration removes the first queue from the rotation).
- `IO_SimpleConnectAndTransmitDataMessageDispatcherOutgoingTests`,
  `IO_SslSimpleConnectAndTransmitDataMessageDispatcherOutgoingTests`, `IO_MessagingClientTests`
  and `IO_MessagingMultiplexingTests` (several connections under one fixed peer id from one host,
  uniform distribution asserted) cover the unchanged cooperative behaviour end to end.

---

## M-4 and the `sourcePeerId` fill-only note: deferred with the mutual-TLS item (2026-09-07)

**Decision (binding, 2026-09-06, decision 1 of the remaining-work handoff):** M-4 and the
`sourcePeerId` fill-only note carried inside M-3 are **deferred with item 3 above** (mutual TLS on
the outbound port with the peer id derived from the client certificate). Record only; no code was
written for either in the 2026-09-06/07 implementation of the review.

### Why they belong here rather than in a fix of their own

M-4's central rule is `sourcePeerId == m_sourcePeerId`, i.e. a peer may route only to *itself*.
That rule is only worth what the connection's own id is worth, and today the id is self-declared
on both ports (this record, "How the outbound port identifies a peer today"). Landing the check
against an unauthenticated id would move the spoofing one hop - an attacker declares the victim's
id at connect time and then passes the `sourcePeerId` check trivially - while breaking any
deployment whose proxy does not authenticate. Once the deferred fix binds the peer id to a client
certificate, M-4's rule and the `sourcePeerId` note both reduce to "compare against the
authenticated identity" and are enforced against the same thing.

### The two findings, for the session that picks this up

| Finding | Location | What is wrong | Fix when picked up | How to validate |
|---|---|---|---|---|
| M-4 | `messaging/BrokerBackendProcessing.h:312-394` (both ids taken from the client JSON; the association is stored when the target is not directly connected), `:398-423` (backend-only messages skip the principal/token check entirely), `:48-75` (`PeerIdRoutingCacheT` has no size bound and no expiry) | any client on the inbound port can map `victimTarget -> attackerPhysicalPeer` for every peer behind a proxy, so `Dispatch` delivers the victim's messages to the attacker; can dissociate any proxied peer (denial of service); and can insert unlimited unique keys (memory exhaustion). The design intent of `:330-340` is only the stale-association race from the *trusted* proxy | require an authorized principal before `Process` for association messages (`principalIdentityInfo` mandatory); accept only `sourcePeerId == m_sourcePeerId`, now the certificate-derived id; cap `m_routingTable` (per-source cap) and evict on `peerDisconnectedNotify`. ~40 lines | 6 units of `utf_baselib_messaging` plus a proxy configuration that authenticates: association from an unauthenticated peer refused; association with a foreign `sourcePeerId` refused; the routing table stops growing at the cap and is emptied for a peer on disconnect |
| M-3 `sourcePeerId` fill-only note (the full plan's M-15) | `messaging/MessagingUtils.h:213-217` | `sourcePeerId` is filled in only when the incoming message left it empty, so a peer that sets it explicitly keeps whatever value it chose; the broker then stamps its authorized principal on a message carrying a foreign source id | overwrite unconditionally with the connection's authenticated id (or reject a mismatch), together with M-4's rule so both use one identity | the same 6 units, plus a client that sets `sourcePeerId` to a foreign id and must be rejected (or corrected) rather than forwarded |

### Interim exposure

Unchanged from "What option (b) does not protect against" and the trust assumption above: the
inbound and outbound ports must be reachable only by hosts trusted to declare their own peer id.
M-4 adds two consequences to that assumption which option (b) does not touch - proxy routes for
*any* peer can be redirected or removed by any peer that can reach the inbound port, and the
routing table is unbounded. R-6 (implemented 2026-09-06) caps a different table
(`m_requestsInFlight` in the REST bridge) and deliberately left its optional `sourcePeerId` check
out for this same reason.

### Revisit conditions

The conditions listed under "Revisit conditions" above govern these two as well; the second of
them ("the M-4 decision requires an authorized principal for association messages") is now
answered: it does, and that is why M-4 waits here.
