# drone: a FreeRTOS + lwIP MQTT end-device

A small FreeRTOS networking end-device that runs as a node in a [qeneth][1]
virtual-network lab alongside [Infix][2] OS instances.  It speaks MQTT:
publishing test patterns and responds to a handful of commands.

qeneth wires QEMU instances together with `-netdev socket,udp=…` links (one
raw Ethernet frame per UDP datagram), and a node's launcher can be anything.
So `drone` joins a topology as a plain process: a custom lwIP network
interface opens the UDP socket pair qeneth assigns and moves L2 frames over
it.  For details, see [doc/qeneth.md][3].

```
   drone (this repo)                  broker node (Infix VM)
  ┌────────────────────────────┐     ┌────────────────────────┐
  │ FreeRTOS POSIX port + lwIP │ L2  │ mosquitto              │
  │ + lwIP apps/mqtt           │◀═══▶│                        │
  │ custom UDP-socket netif    │ UDP │ … more Infix nodes …   │
  └────────────────────────────┘     └────────────────────────┘
```

## Build & Run

```sh
git clone --recurse-submodules <this-repo>   # or: git submodule update --init
make
./build/drone --help
make test                                    # run the self-tests (ping + MQTT)
```

The device attaches to one UDP-socket link (mirroring QEMU's
`-netdev socket,udp=PEER,localaddr=LOCAL`) and takes a static IPv4 config:

```
--localaddr HOST:PORT   bind the link socket here   (default 127.0.0.1:20000)
--udp HOST:PORT         send frames to this peer    (default 127.0.0.1:20001)
--ip / --netmask / --gw static IPv4 (omit --ip to claim a 169.254/16 via AutoIP)
--mac XX:..             interface MAC               (default 02:00:00:00:00:02)
--hostname NAME         device id / role            (default: drone-XXYYZZ from MAC)
--ping ADDR [COUNT]     send ICMP echo after bring-up (built-in diagnostic)
```

Each interface also acquires an IPv6 link-local (`fe80::EUI64`) on bring-up,
derived from the MAC. The device answers ICMP echo on its own, so any peer
can `ping` it.  For a check without qeneth, run two instances back to back:

```sh
utils/run-pair.sh 3
# ping 10.0.0.1: 3 request(s)
# ping: reply from 10.0.0.1: seq=1 time=2 ms
# ...
# ping: 3 sent, 3 received, 0% loss
```

### MQTT

The device publishes JSON telemetry on `dev/<id>/telemetry`, subscribes to
`dev/<id>/cmd`, and replies on `dev/<id>/resp`. Commands:

| command                               | effect                                                |
|---------------------------------------|-------------------------------------------------------|
| `ping`                                | replies `pong`                                        |
| `status`                              | replies uptime / rate / pattern / led / publish count |
| `rate <ms>`                           | set telemetry interval (10..60000)                    |
| `pattern <ramp\|sine\|random\|const>` | choose the test pattern                               |
| `led <on\|off>`                       | toggle the (stubbed) LED, reflected in telemetry      |
| `reboot`                              | acknowledged (no-op in the simulator)                 |

For a demo without an external broker, one instance runs a built-in minimal
test broker (`--run-broker`) that drives a command sequence:

```sh
utils/run-mqtt.sh
# test-broker: <- [dev/dev01/telemetry] {"seq":6,"val":183,"pattern":"sine","led":0}
# test-broker: -> cmd "led on"
# test-broker: <- [dev/dev01/resp] led=on
```

To point the device at a real broker instead, use `--broker ADDR[:PORT]`
(default `10.0.0.1:1883`).  In the qeneth lab the broker is mosquitto on
an Infix node.

## Layout

| Path                            | Description                                            |
|---------------------------------|--------------------------------------------------------|
| doc/qeneth.md                   | how to run drone as a qeneth node                      |
| doc/porting.md                  | notes for porting to Cortex-M7 / S32K3                 |
| include/FreeRTOSConfig.h        | FreeRTOS config for the POSIX port                     |
| lib/FreeRTOS-Kernel             | submodule, pinned V11.3.0                              |
| lib/lwip                        | submodule, pinned STABLE-2_2_1_RELEASE (incl. contrib) |
| port/lwip/lwipopts.h            | lwIP options; arch/cc.h is the platform shim           |
| port/lwip/qeneth_netif.c        | custom lwIP netif over a host UDP socket               |
| qeneth/templates/drone.mustache | qeneth node launcher template                          |
| qeneth/topology.dot.in          | example qeneth topology                                |
| src/main.c                      | entry: parse args, start scheduler                     |
| src/net.c                       | config + lwIP/interface bring-up                       |
| src/ping.c                      | built-in ICMP echo diagnostic                          |
| src/mqtt_app.c                  | MQTT client: telemetry + command handling              |
| src/test_broker.c               | minimal MQTT broker test fixture (--run-broker)        |
| utils/run-pair.sh               | two-instance back-to-back ping self-test               |
| utils/run-mqtt.sh               | device + test-broker MQTT self-test                    |
| Makefile                        | gcc + make build                                       |

[1]: https://github.com/wkz/qeneth
[2]: https://github.com/kernelkit/infix
[3]: doc/qeneth.md
