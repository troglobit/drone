# drone — FreeRTOS + lwIP MQTT end-device

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
So `drone` joins a topology as a plain process: a custom lwIP network
interface opens the UDP socket pair qeneth assigns and tx/rx's L2 frames over
it. No ARM emulation, no extra VM, no TAP/root required. See
[docs/qeneth-integration.md](docs/qeneth-integration.md).

```
  drone (this repo)                  broker node (Infix VM)
  ┌────────────────────────────┐     ┌────────────────────────┐
  │ FreeRTOS POSIX port + lwIP  │ L2  │ mosquitto              │
  │ + lwIP apps/mqtt            │◀═══▶│                        │
  │ custom UDP-socket netif     │ UDP │ … more Infix nodes …   │
  └────────────────────────────┘     └────────────────────────┘
```

## Build & run

```sh
git clone --recurse-submodules <this-repo>   # or: git submodule update --init
make
./build/drone --help
```

The device attaches to one UDP-socket link (mirroring QEMU's
`-netdev socket,udp=PEER,localaddr=LOCAL`) and takes a static IPv4 config:

```
--localaddr HOST:PORT   bind the link socket here   (default 127.0.0.1:20000)
--udp HOST:PORT         send frames to this peer    (default 127.0.0.1:20001)
--ip / --netmask / --gw static IPv4 config          (default 10.0.0.2/24, gw .1)
--mac XX:..             interface MAC               (default 02:00:00:00:00:02)
--hostname NAME         device id                   (default drone)
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

### MQTT

The device publishes JSON telemetry on `dev/<id>/telemetry`, subscribes to
`dev/<id>/cmd`, and replies on `dev/<id>/resp`. Commands:

| command | effect |
|---------|--------|
| `ping` | replies `pong` |
| `status` | replies uptime / rate / pattern / led / publish count |
| `rate <ms>` | set telemetry interval (10..60000) |
| `pattern <ramp\|sine\|random\|const>` | choose the test pattern |
| `led <on\|off>` | toggle the (stubbed) LED, reflected in telemetry |
| `reboot` | acknowledged (no-op in the simulator) |

For a self-contained demo (no external broker), one instance runs a built-in
minimal **test broker** (`--run-broker`) that drives a command sequence:

```sh
scripts/run-mqtt.sh
# test-broker: <- [dev/dev01/telemetry] {"seq":6,"val":183,"pattern":"sine","led":0}
# test-broker: -> cmd "led on"
# test-broker: <- [dev/dev01/resp] led=on
```

To point the device at a real broker instead: `--broker ADDR[:PORT]` (default
`10.0.0.1:1883`). In the qeneth lab the broker is mosquitto on an Infix node.

## Prerequisites

| Tool | Needed for | Status on first setup |
|------|------------|-----------------------|
| `gcc`, `make`, `git` | building this tree | required now |
| `mosquitto` + clients | *optional* real-broker test (built-in fixture needs none) | optional |
| `qeneth`, `mustache`, graphviz | running in a qeneth lab — see [docs/qeneth-integration.md](docs/qeneth-integration.md) | optional |

## Layout

```
include/FreeRTOSConfig.h   FreeRTOS config for the POSIX port
port/lwip/lwipopts.h       lwIP options; arch/cc.h is the platform shim
port/lwip/qeneth_netif.c   custom lwIP netif over a host UDP socket
src/main.c                 entry: parse args, start scheduler
src/net.c                  config + lwIP/interface bring-up
src/ping.c                 built-in ICMP echo diagnostic
src/mqtt_app.c             MQTT client: telemetry + command handling
src/test_broker.c          minimal MQTT broker test fixture (--run-broker)
scripts/run-pair.sh        two-instance back-to-back ping self-test
scripts/run-mqtt.sh        device + test-broker MQTT self-test
qeneth/templates/drone.mustache  qeneth node launcher template
qeneth/topology.dot.in     example qeneth topology
docs/qeneth-integration.md how to run drone as a qeneth node
lib/FreeRTOS-Kernel        submodule, pinned V11.3.0
lib/lwip                   submodule, pinned STABLE-2_2_1_RELEASE (incl. contrib)
Makefile                   gcc + make build
```

## Roadmap

- [x] **M1** — runnable FreeRTOS skeleton (POSIX port, scheduler + tasks).
- [x] **M2** — lwIP up on a custom qeneth UDP-socket netif; ICMP ping both ways.
- [x] **M3** — MQTT: publish test patterns; commands `ping/status/rate/pattern/led/reboot`.
- [x] **M4 artifacts** — qeneth node template + example topology + integration
  guide ([docs/qeneth-integration.md](docs/qeneth-integration.md)). The live
  multi-node lab is wired up in the **Dragnet** repo.
