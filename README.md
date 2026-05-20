# chronostate-engine

A low-latency spatial synchronisation engine written in **C++20**, modelled after the network architecture of legacy side-scrolling MMORPGs (MapleStory). Multiple clients control colored squares that move across a shared 2D platform environment, with the server running as the authoritative simulation.

![Platform layout with two connected players](https://placeholder.invalid/placeholder)

---

## Features

- **Custom binary TCP protocol** — fixed 4-byte header framing (length + opcode), raw little-endian payload. No JSON, no HTTP, no third-party serialisation.
- **Stream defragmentation** — a `StreamBuffer` accumulator safely reassembles packets split across multiple TCP segments.
- **Epoll-based async I/O** (edge-triggered) — the server handles N clients on a single network thread without blocking.
- **Actor-model zone loop** — game state lives exclusively on one dedicated 60 Hz thread. The network thread pushes tasks into a lock-protected queue; the zone thread drains and executes them sequentially, requiring no locks on game state.
- **Server-authoritative physics** — gravity (980 px/s²), foothold snap, and speed anti-cheat run on the server. Clients that teleport receive a `ServerCorrect` snap-back.
- **Change-driven replication** — entities carry a dirty flag. `ServerReplicate` packets are only emitted when state actually changed, and only include the changed subset. An idle session produces zero packets.
- **Client-side prediction** — the local player runs physics locally for zero perceived input lag. The server overrides only on cheat detection.
- **Entity interpolation** — remote players lerp smoothly between server ticks instead of snapping.
- **Raylib visualiser** — hardware-accelerated 2D window showing footholds, all players, and a live HUD.

---

## Architecture

```
chronostate-engine/
├── CMakeLists.txt
└── src/
    ├── Core/
    │   ├── Packet.hpp        # PacketWriter / PacketReader / StreamBuffer
    │   ├── Opcodes.hpp       # Opcode enum + logPkt() utility
    │   └── Types.hpp         # Vector2D, Foothold, footholdYAt()
    ├── Server/
    │   ├── Main.cpp          # Entry point — wires Zone ↔ NetworkLoop
    │   ├── NetworkLoop.hpp/cpp  # Epoll async TCP, per-session StreamBuffer
    │   ├── Zone.hpp/cpp      # 60 Hz actor loop, physics, dirty-flag broadcast
    │   └── Entity.hpp/cpp    # Entity POD struct
    └── Client/
        ├── Main.cpp          # Raylib window loop, input, rendering
        ├── NetworkWorker.hpp/cpp  # Background TCP thread, packet queue
        └── Physics.hpp/cpp   # Fixed-step gravity + foothold collision
```

---

## Protocol

All packets travel over a raw TCP stream with this framing:

```
┌──────────────┬──────────────┬────────────────────────────┐
│  Length u16  │  Opcode u16  │  Payload (little-endian)   │
│  (2 bytes)   │  (2 bytes)   │  (Length − 4 bytes)        │
└──────────────┴──────────────┴────────────────────────────┘
```

| Opcode | Direction | Payload |
|--------|-----------|---------|
| `0x0001` ClientHello | C → S | `u8 r, g, b` |
| `0x0002` ClientMove | C → S | `f32 x, y, vx, vy` |
| `0x0010` ServerAssignId | S → C | `u32 id, u8 r,g,b, f32 x,y, u32 numFH, FH[]` |
| `0x0011` ServerReplicate | S → C | `u32 count, (u32 id, f32 x,y,vx,vy)[]` |
| `0x0012` ServerCorrect | S → C | `f32 x, y` |
| `0x0013` ServerJoin | S → C | `u32 id, u8 r,g,b, f32 x,y` |
| `0x0014` ServerLeave | S → C | `u32 id` |

---

## Building

### Prerequisites

| Dependency | Notes |
|------------|-------|
| CMake ≥ 3.20 | |
| GCC or Clang with C++20 | |
| libX11, libXrandr, libXi, libGL | Raylib X11 deps (usually already present) |
| Internet access on first build | CMake fetches Raylib 5.0 via FetchContent |

### Steps

```bash
git clone https://github.com/YOUR_USERNAME/chronostate-engine
cd chronostate-engine
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build . --parallel
```

This produces two binaries in `build/`:

| Binary | Role |
|--------|------|
| `server` | Headless zone server (port 7777) |
| `client` | Raylib visualiser window |

---

## Running

**Terminal 1 — server:**
```bash
./build/server
```

**Terminal 2+ — one client per player:**
```bash
./build/client                  # connect to localhost
./build/client 192.168.1.x      # connect to remote host
./build/client 192.168.1.x 7777 # explicit host + port
```

Controls:

| Key | Action |
|-----|--------|
| `A` / `←` | Move left |
| `D` / `→` | Move right |
| `W` / `↑` / `Space` | Jump |

---

## How It Works

### Connection flow

```
Client                          Server (Zone thread)
──────                          ────────────────────
TCP connect
ClientHello [r,g,b]  ────────►  addEntity_()
                     ◄────────  ServerAssignId [id, spawn, footholds]
                     ◄────────  ServerJoin [existing entity…]  (for each)
                     ◄────────  ServerJoin [new entity]        (broadcast)
```

### Movement cycle (per 60 Hz tick)

```
Client                          Server
──────                          ──────
physics.update(dt)              ← local, no lag
if state changed:
  ClientMove [x,y,vx,vy] ────► updateMove_() validates speed
                                applyPhysics_() — gravity + snap
                                if dirty:
                     ◄────────  ServerReplicate [changed entities only]
remote.lerpT → 1.0              ← smooth interpolation over 50 ms
```

### Dirty-flag replication

An entity is marked dirty when:
- `applyPhysics_` computes a new position different from the previous one
- `updateMove_` accepts a client position report

`ServerReplicate` is skipped entirely when no entity is dirty. An idle session with all players standing still produces **zero** replication packets.

---

## Console Logging

Every packet sent and received is printed to stdout on both the server and client:

```
[SERVER RECV] fd=5 | ClientMove      (0x0002) 20B
[SERVER SEND] fd=5 | ServerReplicate (0x0011) 28B
[CLIENT RECV]      | ServerReplicate (0x0011) 28B
```

---

## License

MIT
