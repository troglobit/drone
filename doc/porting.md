# Porting `drone` to real silicon (Cortex-M7 / NXP S32K3)

The POSIX-simulator build is the functional and protocol test target. Moving to
the real MCU keeps the application intact and swaps out the platform layers.

## What carries over unchanged

- The application logic in `mqtt_app.c` (telemetry patterns and command
  handling), the topic scheme, and the command set: all plain portable C.
- The lwIP core and its FreeRTOS `sys_arch` port. The sys_arch is already
  FreeRTOS-based rather than POSIX, so it works as-is on the MCU.
- The `heap_4` allocator, already used here; just size `configTOTAL_HEAP_SIZE`
  to the available SRAM.
- Most of `lwipopts.h` (protocol selection, MQTT, buffer counts).

## What changes

- Replace the kernel port `portable/ThirdParty/GCC/Posix` with the Cortex-M7
  port (`portable/GCC/ARM_CM7/r0p1`), and add the S32K3 startup code, vector
  table, and linker script from the NXP S32K3 SDK / RTD. Set
  `configCPU_CLOCK_HZ` and let SysTick drive the tick. The
  `configENABLE_BACKWARD_COMPATIBILITY` shim can stay.
- Build with `arm-none-eabi-gcc`, the linker script, and `-mcpu=cortex-m7` plus
  FPU flags, instead of host `gcc -pthread`.
- Replace `port/lwip/qeneth_netif.c` (frames over a host UDP socket) with a
  driver for the S32K3 ENET/GMAC: DMA descriptor rings, with the RX interrupt
  handing buffers to lwIP via `tcpip_input` from an ISR-deferred task. The RX
  path becomes interrupt- and DMA-driven instead of the simulator's
  non-blocking poll (see the note in `qeneth_netif.c`). If the MAC offloads
  checksums, set the `CHECKSUM_GEN_*`/`CHECKSUM_CHECK_*` options to match.
- Retarget logging: the `printf` calls need a `_write()` that goes to a UART (or
  SWO/RTT), since there is no host stdout.
- Replace the CLI args (`--ip`, `--mac`, `--broker`, and so on) with
  compile-time config or a settings store, since there is no `argv`.
- Set `MEM_ALIGNMENT` to 4 on the 32-bit target (it is 8 here for the 64-bit
  host).
- For a production broker, enable `LWIP_ALTCP_TLS` with mbedTLS; it is `0` in
  this build.
