# frtos-dev — FreeRTOS + lwIP MQTT end-device

A small FreeRTOS networking end-device used as a node in a
[qeneth](https://github.com/wkz/qeneth) virtual-network lab alongside
[Infix](https://github.com/kernelkit/infix) OS instances. It speaks MQTT:
it publishes test patterns and responds to a handful of commands.

This repository is the **functional / protocol test target**. It builds with
plain `gcc` + `make` and runs as an ordinary x86_64 Linux process using the
FreeRTOS-Kernel **POSIX/Linux simulator port** plus **lwIP**. That keeps the
whole lab x86_64 (uniform KVM acceleration with the Infix nodes) while still
exercising the real RTOS application logic and the real lwIP stack.

> The eventual hardware target is a **Cortex-M7 (NXP S32K3)**. That is a much
> later phase and is **not** modelled here — this rig validates communication,
> protocols, and overall system behaviour, not the silicon.

## How it plugs into qeneth

qeneth wires QEMU instances together with `-netdev socket,udp=…` links — one
raw Ethernet frame per UDP datagram — and a node's launcher "can be anything".
So `frtos-dev` joins a topology as a plain process: a custom lwIP network
interface opens the UDP socket pair qeneth assigns and tx/rx's L2 frames over
it. No ARM emulation, no extra VM, no TAP/root required. *(Arrives in M2/M4.)*

```
  ┌──────────────────────────────┐   UDP link  ┌───────────────────────────┐
  │ frtos-dev (this repo)         │  :20000 ↔   │ infix-gw (x86_64, KVM)     │
  │ native x86_64 process         ●─────────────● + mosquitto broker         │
  │ FreeRTOS POSIX port + lwIP    │  :20001     │   … more Infix nodes …     │
  │ + lwIP apps/mqtt              │             │                           │
  └──────────────────────────────┘             └───────────────────────────┘
```

## Build & run

```sh
git clone --recurse-submodules <this-repo>   # or: git submodule update --init
make
make run            # or: ./build/frtos-dev   (Ctrl-C to stop)
```

Expected output (Milestone 1):

```
frtos-dev: FreeRTOS kernel V11.3.0, POSIX simulator
[frtos-dev] hello #0  (worker=0, uptime=1 ms)
[frtos-dev] hello #1  (worker=4, uptime=1001 ms)
...
```

## Prerequisites

| Tool | Needed for | Status on first setup |
|------|------------|-----------------------|
| `gcc`, `make`, `git` | building this tree | required now |
| `mosquitto` + clients | MQTT broker & test (`mosquitto_pub/sub`) | from M3 |
| `qeneth`, `mustache`, graphviz (`gvpr`,`dot`) | running the topology | from M4 |

Install on Debian/Ubuntu (later milestones):
`sudo apt install mosquitto mosquitto-clients graphviz ruby-mustache`

## Layout

```
include/FreeRTOSConfig.h   FreeRTOS config for the POSIX port
src/main.c                 application entry / tasks
lib/FreeRTOS-Kernel        submodule, pinned V11.3.0
lib/lwip                   submodule, pinned STABLE-2_2_1_RELEASE
Makefile                   gcc + make build
```

## Roadmap

- [x] **M1** — runnable FreeRTOS skeleton (POSIX port, scheduler + tasks).
- [ ] **M2** — lwIP up on a custom qeneth UDP-socket netif; ICMP ping.
- [ ] **M3** — MQTT: publish test patterns; commands `ping/status/rate/pattern/led/reboot`.
- [ ] **M4** — qeneth node template + topology; end-to-end with Infix + mosquitto.

### MQTT scheme (planned)

- publishes telemetry/test patterns on `dev/<id>/telemetry`
- subscribes to `dev/<id>/cmd`, replies on `dev/<id>/resp`
- commands: `ping`, `status`, `rate <ms>`, `pattern <ramp|sine|random|const>`, `led <on|off>`, `reboot`
