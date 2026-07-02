# VxWorks 7 RTP build

Apple-1 can run as a **VxWorks Real Time Process** on the QEMU x86-64 SDK from
[Wind River Labs](https://labs.windriver.com/vxworks-sdk/) (NCLA / educational
use only).

## Host setup

```bash
source ~/vxworks-sdk/sdkenv.sh
```

Confirm `wr-cc` is on `PATH`.

## Build

From the emulator tree:

```bash
bash vxworks_rtp_build.sh
```

Produces **`apple1.vxe`** and **`vxsmoke.vxe`** in the repo root.

The RTP build uses **`term_vt100.c`** (teletype output — plain text on a serial
line).  Do not use `term_ansi.c` on QEMU `-serial stdio`; the ANSI full-screen
redraw looks like garbage on a dumb serial console.

The build also uses a smaller footprint (`8` KB static RAM buffer, `512`-byte
keyboard ring) and omits debugger, ACI, and Krusader.

`port_vxworks.c` uses VxWorks `tickGet` / `taskDelay` for the speed throttle
and RTP `termios` + stdio for the terminal and VFS.  SIGINT handler install is
disabled on RTP (faulted on early QEMU SDK builds).

## Boot VxWorks (QEMU)

### FTP

Expose your home directory (user `target`, password `vxTarget`):

```bash
sudo python3 -m pyftpdlib -p 21 -u target -P vxTarget -d "$HOME"
```

### QEMU without killing the VM on Ctrl-C

Plain `-serial stdio` sends **Ctrl-C to QEMU** and exits the whole VM.

From the emulator tree:

```bash
bash vxworks_qemu.sh
```

That uses QEMU `chardev … signal=off` so **^C goes to VxWorks**, not the host.

**Or** put serial on TCP and use a second terminal (also isolates ^C):

```bash
SERIAL=tcp:127.0.0.1:5555,server,nowait bash vxworks_qemu.sh
telnet 127.0.0.1 5555
```

At the kernel shell: `cmd` for the command interpreter.

## Deploy and run

If the FTP root is your home directory and the repo is
`~/git/apple1-emu`, VxWorks sees it as `host.host/git/apple1-emu`.

```
[vxWorks *]# cd /host.host/git/apple1-emu
[vxWorks *]# ls
```

### Smoke test

```
[vxWorks *]# vxsmoke.vxe
```

### Emulator (interactive serial)

Use **`rtp exec -i`** so stdin is attached to the RTP (required for keyboard
input).  Without `-i`, the cmd shell keeps line mode and echoes every key.

```
[vxWorks *]# rtp exec -i apple1.vxe
```

The port disables VxWorks tty **driver echo** (`ioctl` `OPT_RAW`) as well as
POSIX `termios` echo.  If keys still appear twice, rebuild after pulling the
latest `port_vxworks.c` and redeploy `apple1.vxe`.

Press **Ctrl+R** once to reset into the Woz Monitor if you did not auto-reset
(older builds).  **Ctrl+L** clears the teletype buffer; **Ctrl+R** resets the
6502 (these are swallowed by the emulator, not printed as `^L` / `^R`).

Headless (no terminal I/O):

```
[vxWorks *]# apple1.vxe -H
```

### Larger RTP stack (optional)

```
[vxWorks *]# rtp exec -u 262144 -- apple1.vxe
```

Use `--` before app arguments if you pass flags like `-H`.

### Quit apple1

SIGINT is not hooked on VxWorks RTP.  Stop the RTP from `->`:

```
-> rtp show
-> rtp delete <process-id>
```

Or close the telnet session if you use the TCP serial option.

## Notes

- Wind River's **NCLA** governs your use of VxWorks, not the emulator source
  (Unlicense). Do not redistribute the SDK or ship commercial products on NCLA.
- ROMs under `roms/` remain third-party; see `COPYING`.
