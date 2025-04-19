# XRPC Design

XRPC is a modular RPC framework inspired by KRPC and gRPC. It consists of:
- **Channel**: Client-side communication.
- **Server**: Service hosting and request handling.
- **Controller**: Call state and error management.
- **Registry**: Service discovery via ZooKeeper.
- **Codec**: Protocol encoding/decoding.

(TBD: Detailed design after implementation)