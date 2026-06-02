# drone: a simple FreeRTOS + MQTT end-device

A small FreeRTOS networking end-device for running as a node in a [qeneth][1]
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
make check                                   # run the self-tests (autotools style)
```

The device attaches to one UDP-socket link (mirroring QEMU's
`-netdev socket,udp=PEER,localaddr=LOCAL`) and takes a static IPv4 config:

```
--localaddr HOST:PORT   bind the link socket here   (default 127.0.0.1:20000)
--udp HOST:PORT         send frames to this peer    (default 127.0.0.1:20001)
--ip / --netmask / --gw static IPv4 (omit --ip for AutoIP, or --dhcp)
--dhcp                  request a DHCP lease; AutoIP fallback if no server
--mac XX:..             interface MAC               (default 02:00:00:00:00:02)
--hostname NAME         device id / role            (default: drone-XXYYZZ from MAC)
--ping ADDR [COUNT]     send ICMP echo after bring-up (built-in diagnostic)
```

With `--dhcp` the device requests a lease from a DHCP server on the L2;
`--hostname` is advertised as DHCP option 12 (Host Name) so a server like
dnsmasq can hand out a static lease keyed on the drone name
(`dhcp-host=drone-north-pump,10.0.0.50`).  RFC 3927 cooperative AutoIP
takes over after two failed DHCP tries (~12 s) if no server answers, so
`--dhcp` is safe to set unconditionally — the drone still comes up on
169.254/16 in an isolated lab.

Each interface also acquires an IPv6 link-local (`fe80::EUI64`) on bring-up,
derived from the MAC, and the device announces itself via mDNS as
`<hostname>.local` plus a `_drone._tcp` service so a CNC can browse for it.
The device answers ICMP echo on its own, so any peer can `ping` it.  For a
check without qeneth, run two instances back to back:

```sh
test/pair.sh 3
# ping 10.0.0.1: 3 request(s)
# ping: reply from 10.0.0.1: seq=1 time=2 ms
# ...
# ping: 3 sent, 3 received, 0% loss
```

### MQTT

The device publishes JSON telemetry on `dev/<id>/telemetry`, subscribes to
`dev/<id>/cmd`, and replies on `dev/<id>/resp`. Commands:

| command                               | description                                           |
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
test/mqtt.sh
# test-broker: <- [dev/dev01/telemetry] {"seq":6,"val":183,"pattern":"sine","led":0}
# test-broker: -> cmd "led on"
# test-broker: <- [dev/dev01/resp] led=on
```

By default the device discovers its broker over LLDP: a neighboring switch
(or any LLDP-speaking peer) advertising a Management-Address TLV is taken
to be the broker, and the device connects to that IPv4 on port 1883.
Until a frame arrives the device logs `waiting for LLDP from neighboring
MQTT broker` once every 10 s.  drone also sends its own LLDP frames every
30 s so each switch shows it in `lldpcli show neighbors`, which doubles as
a free fleet inventory.

`--broker ADDR[:PORT]` overrides discovery.  ADDR is either an IPv4 dotted-
decimal or a `*.local` hostname that drone resolves via mDNS (one-shot
A-record query with the QU bit, see [src/mdns_resolve.c][4]).  The wait
loop retries indefinitely so a slow Linux/Infix peer that needs ~30 s to
bring up `lldpd` or `avahi` is handled naturally — drone just keeps trying
until the upstream answers.

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
| port/lwip/raw_ethertype.c       | EtherType dispatch (LLDP consumes 0x88CC before IP)    |
| qeneth/templates/drone.mustache | qeneth node launcher template                          |
| qeneth/topology.dot.in          | example qeneth topology                                |
| src/main.c                      | entry: parse args, start scheduler                     |
| src/net.c                       | config + lwIP/interface bring-up                       |
| src/ping.c                      | built-in ICMP echo diagnostic                          |
| src/mqtt_app.c                  | MQTT client: telemetry + command handling              |
| src/lldp.c                      | LLDP TX/RX; broker addr from neighbor Management TLV   |
| src/mdns_resolve.c              | one-shot mDNS A-record querier (`*.local` -> IPv4)     |
| src/test_broker.c               | minimal MQTT broker test fixture (--run-broker)        |
| test/                           | self-test scripts (one per slice); test.mk is included |
| utils/drone.sh                  | shared shell helpers (drone_require_bin, drone_run_bg) |
| utils/lldp_frame.py             | synthetic LLDP injector (for negative/edge-case tests) |
| Makefile / .clang-format        | gcc + make build, plus `make fmt` for Linux KNF        |

[1]: https://github.com/wkz/qeneth
[2]: https://github.com/kernelkit/infix
[3]: doc/qeneth.md
[4]: src/mdns_resolve.c
