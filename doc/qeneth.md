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
| `qn_ip`      | *(none, AutoIP)* | static IPv4 address; omit for AutoIP      |
| `qn_gw`      | *(none, AutoIP)* | default gateway                           |
| `qn_broker`  | *(none, LLDP)*   | MQTT broker; omit to discover via LLDP    |
| `qn_extra`   | *(none)*         | extra flags, e.g. `--run-broker`          |

> If `qn_ip` is set, the drone uses that static address; otherwise it claims a
> link-local 169.254/16 address via AutoIP (RFC 3927). DHCP is not built in
> yet — enable `LWIP_DHCP` if you need it. The netif also obtains an IPv6
> `fe80::EUI64` link-local automatically (SLAAC, derived from the MAC).

## Where the broker lives

By default the device discovers its broker from LLDP: a neighbor advertising
a Management-Address TLV is taken to be the broker, and the device connects
to that IPv4 on port 1883.  Infix's `lldpd` advertises the node's mgmt IP
out of the box, so no extra configuration is needed for the lab case.  Two
common setups:

- Real broker (the lab default): run `mosquitto` on an Infix or other Linux
  node; its `lldpd` advertises the node's mgmt IP and drone picks it up
  automatically.  This is the Dragnet setup.  Set `qn_broker` to override
  (skips discovery), e.g. `qn_broker="10.0.0.1:1883"` while bringing things
  up.
- Self-contained (no Infix or mosquitto): make the peer a second `drone` with
  `qn_template="drone"`, `qn_extra="--run-broker"`, `qn_ip="10.0.0.1"`.  Both
  drones send LLDP, the device side learns the broker IP and connects.

`qn_broker` accepts either an IPv4 dotted-decimal (`10.0.0.1:1883`) or a
`*.local` hostname (`broker1.local:1883`).  Hostnames are resolved via a
one-shot mDNS A-record query against the upstream peer's mDNS responder, so
no DNS server is required.

> **Slow upstream tolerance:** Linux/Infix nodes typically take ~30 s to
> bring up `lldpd` and the mDNS responder, while a drone reaches the wait
> loop in well under a second.  drone keeps polling indefinitely (every
> 1 s for LLDP, every ~3 s for mDNS), logging once every ~10 s, so a
> drone launched at the same instant as its upstream still converges to a
> working connection as soon as the upstream is ready.  No special timing
> dance from the launcher.

## Discovering drones

Every drone announces itself on the qeneth L2 as `<hostname>.local` via
mDNS, plus a `_drone._tcp` service carrying TXT records with the device id,
firmware tag, and configured broker.  A CNC on any node of the same L2 can
browse for drones (`avahi-browse _drone._tcp`) and resolve their names
without out-of-band IP knowledge.  The MAC-derived default hostname makes
fresh drones unique by construction; a `--hostname north-pump` overrides it
with a meaningful role label.

## Without qeneth

For quick local checks you don't need qeneth at all.  The scripts under
`test/` wire two `drone` processes together over a local UDP-socket link
(sharing the helpers in `utils/drone.sh`):

```sh
test/pair.sh     # ICMP ping across the link
test/mqtt.sh     # MQTT telemetry + command round-trip
```

[1]: https://github.com/wkz/qeneth
