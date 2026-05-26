# Running `drone` as a qeneth node

`drone` drops into a [qeneth][1] topology as an ordinary node, but unlike the
Linux/Infix nodes it is not a QEMU instance. It is the native `drone` binary,
and it joins the network the same way qeneth connects QEMU instances: a
`-netdev socket,udp=…` link, where each UDP datagram carries one raw Ethernet
frame. qeneth's docs say a node's launcher can be anything, and that is the
hook drone uses.

This directory holds the artifacts; the live lab runs in the Dragnet repo.

```
qeneth/templates/drone.mustache   node launcher template
qeneth/topology.dot.in            example topology
```

## The launch contract

For each link, qeneth allocates a local UDP port, the peer's UDP port, and a
MAC, and exposes them to the template. They map onto `drone`'s CLI one to one:

| qeneth template var | `drone` argument        | meaning                         |
|---------------------|-------------------------|---------------------------------|
| `{{qn_sport}}`      | `--localaddr localhost:<port>` | bind (receive) UDP port  |
| `{{qn_dport}}`      | `--udp localhost:<port>`       | peer (send) UDP port     |
| `{{qn_mac}}`        | `--mac <xx:..>`         | interface MAC                   |
| `{{name}}`          | `--hostname <name>`     | device id (MQTT topic base)     |

> A drone has exactly one network interface (a single lwIP netif), so give it a
> single link in the topology. The template iterates `{{#links}}`; with more
> than one link it would emit duplicate flags that the binary rejects.

`drone` accepts `localhost` (the host qeneth uses for socket links) as loopback,
so the generated command line works unchanged.

## Setup

1. Build the binary and put it on `$PATH` (or set the `qn_bin` node attribute to
   an absolute path):
   ```sh
   make            # produces ./build/drone
   ```
2. Copy `qeneth/templates/drone.mustache` into your qeneth `templates/`
   directory.
3. Add a node with `qn_template="drone"` to your `topology.dot.in` (see
   `qeneth/topology.dot.in`).
4. `qeneth generate && qeneth start`, then `qeneth console drone`.

### Node attributes (set in `topology.dot.in`)

| attribute    | default          | purpose                                   |
|--------------|------------------|-------------------------------------------|
| `qn_bin`     | `drone` (PATH)   | path to the binary                        |
| `qn_ip`      | `10.0.0.2`       | static IPv4 address                       |
| `qn_gw`      | `10.0.0.1`       | default gateway                           |
| `qn_broker`  | `10.0.0.1:1883`  | MQTT broker address                       |
| `qn_extra`   | *(none)*         | extra flags, e.g. `--run-broker`          |

> `drone` uses a static IP (no DHCP yet). If your lab assigns addresses
> differently, set `qn_ip`/`qn_gw` to match, or build with `LWIP_DHCP`.

## Where the broker lives

The device opens a TCP/MQTT connection to `qn_broker` over the qeneth L2
network; no host TAP or bridging is involved. Two common setups:

- Real broker (the lab default): run `mosquitto` on an Infix or other Linux node
  configured with the broker IP, e.g. `10.0.0.1`. This is the Dragnet setup.
- Self-contained (no Infix or mosquitto): make the peer a second `drone` with
  `qn_template="drone"`, `qn_extra="--run-broker"`, `qn_ip="10.0.0.1"`. It runs
  the built-in test broker and drives a command sequence.

## Without qeneth

For quick local checks you don't need qeneth at all. The `utils/` scripts wire
two `drone` processes together over a local UDP-socket link:

```sh
utils/run-pair.sh     # ICMP ping across the link
utils/run-mqtt.sh     # MQTT telemetry + command round-trip
```

[1]: https://github.com/wkz/qeneth
