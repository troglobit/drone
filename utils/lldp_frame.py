#!/usr/bin/env python3
"""Craft and send one LLDP frame over a qeneth-style UDP-socket link.

Usage:
    lldp_frame.py PEER_HOST PEER_PORT NEIGHBOR_IP SRC_MAC

Sends one LLDPDU to the drone bound on PEER_HOST:PEER_PORT, with a
Management-Address TLV carrying NEIGHBOR_IP (IPv4) and SRC_MAC as the
sender's chassis/port-id.  Used as a synthetic injector for negative or
edge-case tests when you don't want to spin up a peer drone.

Example:
    lldp_frame.py 127.0.0.1 20000 10.0.0.1 02:00:00:00:00:01
"""
import socket
import struct
import sys

LLDP_DST = bytes([0x01, 0x80, 0xC2, 0x00, 0x00, 0x0E])
LLDP_ETHERTYPE = 0x88CC


def tlv(t, v):
    """Pack one LLDP TLV: 7-bit type + 9-bit length + value."""
    hdr = ((t & 0x7F) << 9) | (len(v) & 0x1FF)
    return struct.pack(">H", hdr) + v


def mgmt_addr_v4(ip):
    """Management-Address TLV, IPv4 subtype, with a placeholder ifIndex."""
    addr = socket.inet_aton(ip)
    val = (
        bytes([5])                   # addr-string-length: 1 subtype + 4 IPv4
        + bytes([1])                 # subtype: IPv4
        + addr
        + bytes([2])                 # interface subtype: ifIndex
        + struct.pack(">I", 1)       # interface number
        + bytes([0])                 # OID length: 0 (no OID)
    )
    return tlv(8, val)


def main(argv):
    if len(argv) != 5:
        print(__doc__, file=sys.stderr)
        sys.exit(2)

    peer_host = argv[1]
    peer_port = int(argv[2])
    neighbor_ip = argv[3]
    src_mac = bytes(int(x, 16) for x in argv[4].split(":"))
    if len(src_mac) != 6:
        print("bad MAC", file=sys.stderr)
        sys.exit(2)

    frame = LLDP_DST + src_mac + struct.pack(">H", LLDP_ETHERTYPE)
    frame += tlv(1, bytes([4]) + src_mac)           # Chassis-ID, MAC subtype
    frame += tlv(2, bytes([3]) + src_mac)           # Port-ID, MAC subtype
    frame += tlv(3, struct.pack(">H", 120))         # TTL
    frame += tlv(5, b"injector")                    # System-Name
    frame += mgmt_addr_v4(neighbor_ip)              # Management-Address
    frame += tlv(0, b"")                            # End-of-LLDPDU

    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.sendto(frame, (peer_host, peer_port))
    s.close()


if __name__ == "__main__":
    main(sys.argv)
