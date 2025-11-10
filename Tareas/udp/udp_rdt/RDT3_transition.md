# RDT 3.0 Transition Notes

## Overview
- Original code relied on raw UDP helpers `udp_enviar` and `udp_recibir`, which provided no delivery guarantees.
- New implementation introduces **Reliable Data Transfer (RDT) 3.0 stop-and-wait** across both client and server.
- All existing chat features (registration, private/broadcast messages, file/object transfer, TicTacToe) operate on top of the reliable layer.

## Core Replacements
- `udpCli.cpp` now wraps every outgoing request with `reliableSend()` and every incoming reception with `reliableReceive()`.
- `udpSer.cpp` keeps a per-client map of `RDTState` objects and routes all payloads through `reliableSendTo()` / `rdt_recv()`.
- The old direct calls to `udp_enviar`/`udp_recibir` remain only in legacy helpers (e.g., the original object/file demo functions) for compatibility, while the main workflow uses RDT.

## RDT Structures & Helpers
- `RDTHeader` (type, sequence bit, payload length, checksum) encapsulates metadata.
- `RDTState` tracks `sendSeq`, `expectedSeq`, and the last ACK seen per peer.
- `rdt_send()` handles: header creation, checksum, retransmissions (5 tries, 500 ms timeout), and ACK validation.
- `rdt_recv()` verifies checksum, filters duplicates, acknowledges valid frames, and returns the payload.
- Server-side state lives in `unordered_map<string, RDTState> rdtStates`, keyed by `ip:port`.

## ACK Handling
- Transport ACKs are generated in `rdt_recv()` via `send_ack()`. They contain only the RDT header and acknowledge the sequence bit.
- Application-level confirmations (`K...`) are generated in `udpSer.cpp` after successful deliveries, letting the sender know the payload reached its destination.
- Clients display a local cue (`[ACK local]`) plus the server confirmation (`[ACK servidor] ...`).

## Notable Server Changes
- Main loop peeks the UDP datagram to determine the sender, then invokes `rdt_recv()` under the sender's state lock.
- If `reliableSendTo()` exhausts retries, the server removes the user and cleans up TicTacToe participation.
- Broadcasts and private messages now return `K` notifications to the sender once delivery succeeds.
- TicTacToe engine (`TicTacToe.cpp`) switched entirely to reliable sends for boards, turns, and game-over messages.

## Notable Client Changes
- Shared `RDTState clientRdtState` plus a mutex ensure the stop-and-wait state machine remains consistent with the single-threaded receiver.
- Menu actions (`send`, `list`, file/object commands, TicTacToe) reuse the reliable helpers.
- Registration retries on timeout and warns the user when the server does not respond.

## Testing Checklist
1. Register multiple users; verify duplicates trigger reliable error responses (`E...`).
2. Send private and broadcast messages; observe `[ACK servidor] ...` on the sender and `[ACK local] ...` on the receiver.
3. Transfer a file > 1 KB both directions; confirm chunk counters and final completion message.
4. Send a Mochila object; verify serialized fields and reliable delivery.
5. Launch a TicTacToe invite, accept it, and alternate moves until win/draw; ensure the board updates reliably.
6. Force a client to exit with `Ctrl+C` and trigger a message from another user; confirm the server logs the timeout and allows re-login.

## Review Questions (answer in English)
1. **Where in the code do we calculate and attach the checksum for each RDT frame?**
2. **Which data structure keeps track of the expected sequence number for each connected peer on the server?**
3. **Describe the flow of a private message from sender to receiver, including when the application-level ACK is emitted.**
4. **Why do we protect `reliableSend()` on the client with a mutex? What could happen without it?**
5. **When a TicTacToe move results in a win, which functions issue the final notifications, and how are they sent reliably?**
6. **What cleanup happens if `reliableSendTo()` fails because a client has disappeared?**
