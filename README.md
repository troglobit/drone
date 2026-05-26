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
./build/frtos-dev --help
```

The device attaches to one UDP-socket link (mirroring QEMU's
`-netdev socket,udp=PEER,localaddr=LOCAL`) and takes a static IPv4 config:

```
--localaddr HOST:PORT   bind the link socket here   (default 127.0.0.1:20000)
--udp HOST:PORT         send frames to this peer    (default 127.0.0.1:20001)
--ip / --netmask / --gw static IPv4 config          (default 10.0.0.2/24, gw .1)
--mac XX:..             interface MAC               (default 02:00:00:00:00:02)
--hostname NAME         device id                   (default frtos-dev)
--ping ADDR [COUNT]     send ICMP echo after bring-up (built-in diagnostic)
```

The device answers ICMP echo automatically (so any peer can `ping` it). For a
self-contained check without qeneth, run two instances back-to-back:

```sh
scripts/run-pair.sh 3
# ping 10.0.0.1: 3 request(s)
# ping: reply from 10.0.0.1: seq=1 time=2 ms
# ...
# ping: 3 sent, 3 received, 0% loss
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
port/lwip/lwipopts.h       lwIP options; arch/cc.h is the platform shim
port/lwip/qeneth_netif.c   custom lwIP netif over a host UDP socket
src/main.c                 entry: parse args, start scheduler
src/net.c                  config + lwIP/interface bring-up
src/ping.c                 built-in ICMP echo diagnostic
scripts/run-pair.sh        two-instance back-to-back self-test
lib/FreeRTOS-Kernel        submodule, pinned V11.3.0
lib/lwip                   submodule, pinned STABLE-2_2_1_RELEASE (incl. contrib)
Makefile                   gcc + make build
```

## Roadmap

- [x] **M1** — runnable FreeRTOS skeleton (POSIX port, scheduler + tasks).
- [x] **M2** — lwIP up on a custom qeneth UDP-socket netif; ICMP ping both ways.
- [ ] **M3** — MQTT: publish test patterns; commands `ping/status/rate/pattern/led/reboot`.
- [ ] **M4** — qeneth node template + topology; end-to-end with Infix + mosquitto.

### MQTT scheme (planned)

- publishes telemetry/test patterns on `dev/<id>/telemetry`
- subscribes to `dev/<id>/cmd`, replies on `dev/<id>/resp`
- commands: `ping`, `status`, `rate <ms>`, `pattern <ramp|sine|random|const>`, `led <on|off>`, `reboot`
