# drone

A FreeRTOS + lwIP MQTT end-device.  POSIX today, Cortex-M7 (NXP
S32K3) later.

The device publishes JSON telemetry on `dev/<id>/telemetry`,
subscribes to `dev/<id>/cmd`, and answers on `dev/<id>/resp`.
Address acquisition goes through `--ip`, `--dhcp`, or AutoIP; the
broker is either set with `--broker` or discovered via LLDP.

## Build

```sh
git clone --recurse-submodules <this-repo>
make
make check          # self-tests
```

`./build/drone --help` prints the full flag set.

## Run

Drone sits on one end of a UDP-socket link.  The other end can be a
sibling drone (see [test/mqtt.sh][3] or [test/pair.sh][5]) or a
[qeneth][1] topology -- see [doc/qeneth.md][4] for the qeneth side.

Static IP plus an explicit broker:

```sh
./build/drone --ip 10.0.0.2 --broker 10.0.0.1:1883 --hostname dev01
```

With an [Infix][2] switch upstream, drone can find both its own
address and the broker on its own.  It waits for an LLDP frame,
asks for a DHCP lease, and reads the broker IP from the neighbor's
Management-Address TLV:

```sh
./build/drone --dhcp --lldp --hostname dev01
```

When the broker advertises itself by mDNS:

```sh
./build/drone --ip 10.0.0.2 --broker broker1.local --hostname dev01
```

## MQTT commands

The broker drives the device on `dev/<id>/cmd`:

| command                               | description                                           |
|---------------------------------------|-------------------------------------------------------|
| `ping`                                | replies `pong`                                        |
| `status`                              | replies uptime / rate / pattern / led / publish count |
| `rate <ms>`                           | set telemetry interval (10..60000)                    |
| `pattern <ramp\|sine\|random\|const>` | choose the test pattern                               |
| `led <on\|off>`                       | toggle the (stubbed) LED, reflected in telemetry      |
| `reboot`                              | acknowledged (no-op in the simulator)                 |

## Layout

| Path        | Description                                            |
|-------------|--------------------------------------------------------|
| src/        | application (main, MQTT, LLDP, mDNS, ping)             |
| port/lwip/  | lwIP port glue (UDP-socket netif, EtherType dispatch)  |
| lib/        | pinned FreeRTOS-Kernel + lwIP submodules               |
| doc/        | qeneth integration, porting notes                      |
| test/       | self-tests run by `make check`                         |

[1]: https://github.com/wkz/qeneth
[2]: https://github.com/kernelkit/infix
[3]: test/mqtt.sh
[4]: doc/qeneth.md
[5]: test/pair.sh
