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
(`sourcePeerId` filled only when empty) in the same review; both remain separate decisions and
are not addressed by this record.

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
