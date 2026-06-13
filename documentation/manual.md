# Apple-1 Emulator Documentation Manual

Welcome to the Apple-1 Emulator! This manual describes the emulator features, operation guide, debugger commands, and hardware specifications.

---

## 1. Emulator GUI Controls

The emulator window displays the virtual Apple-1 screen on the left, and a system control sidebar on the right.

### Sidebar Controls
* **Header**: Shows the machine name and the current RAM capacity (configured via `apple1.conf`).
* **RESET**: Hard-resets the emulated 6502 CPU and restarts the execution.
* **CLR SCREEN**: Clears the video display character memory.
* **SPEED**: Toggles CPU speed limits between:
  * **CAPPED**: Runs at the authentic Apple-1 clock rate of **1.022727 MHz** (DRAM cycle-stealing is simulated, and ACI tape timings are cycle-exact).
  * **UNCAPPED**: Emulates as fast as the host processor allows (best for long computations or quick loading).
* **CASSETTE DECK**:
  * Displays the selected tape file (located in the `cassettes/` directory).
  * **`<`** / **`>`**: Cycles through available cassettes in the folder.
  * **SEL TAPE**: Launches a file selector to pick a custom tape file.
  * **PLAY/LOAD**: Play the cassette tape to load program data into the emulated Apple Cassette Interface (ACI).
  * **REC/SAVE**: Records and saves data onto a virtual tape file `recorded_tape.aci`.

---

## 2. Emulator Menu Bar

At the top of the screen is the menu bar containing:
* **FILE**:
  * **LOAD TAPE (.ACI)**: Directly load a tape file.
  * **LOAD BASIC**: Loads the Apple-1 BASIC ROM preset into memory at `$E000-$EFFF`.
  * **LOAD KRUSADER**: Loads the Krusader Assembler preset into memory at `$F000-$FDFF`.
  * **LOAD WOZMON**: Loads the Woz Monitor preset into memory at `$FF00-$FFFF`.
  * **QUIT**: Gracefully exits the emulator.
* **CONFIG**: Opens the Emulator Setup dialog to configure RAM size and ROM paths.
* **DISPLAY**: CRT and Phosphor monitor settings.
* **DEBUG**: Spawns the interactive CLI debugger window.
* **TRACE**: Spawns the real-time execution instruction tracing window (only active when speed is capped).
* **HELP**: Pull up the manuals (this manual or the original Apple-1 Operation Manual PDF).

---

## 3. Interactive CLI Debugger

Selecting **DEBUG** from the menu bar spawns a separate debug terminal window with a `db> ` prompt. The debugger supports the following commands:

* **`s`** (or **`<Enter>`**): Step a single CPU instruction.
* **`c`**: Continue normal CPU execution and close the debugger window.
* **`r`**: Print the current state of all CPU registers (`A`, `X`, `Y`, `SP`, `PC`, `P` flags).
* **`m [start] [end]`**: Hex-dump memory (e.g. `m 0200 0280`).
* **`w [addr] [val]`**: Write a single byte to memory (e.g. `w 0200 EA`).
* **`b [addr]`**: Add a breakpoint at a hex address (e.g. `b 0300`). Typing `b` without an address lists all active breakpoints.
* **`d [addr]`**: Delete a breakpoint at a hex address (e.g. `d 0300`). Typing `d` without an address clears all active breakpoints.
* **`h`** / **`?`**: Print the debugger commands help page.
* **`q`**: Terminate the emulator.

---

## 4. Apple-1 Hardware Specifications

The emulated system is a cycle-accurate model of Wozniak's 1976 Apple-1 design:

### Memory Map
* **`$0000 - $00FF`**: Zero Page (system work area).
* **`$0100 - $01FF`**: System Stack.
* **`$0200 - $02FF`**: Woz Monitor input buffer and variables.
* **`$0300 - $DFFF`**: User RAM (size configured in options).
* **`$C000 - $C0FF`**: ACI Cassette Interface expansion card registers.
* **`$C100 - $C1FF`**: ACI Cassette Interface ROM.
* **`$D010 - $D013`**: Motorola 6820 Peripheral Interface Adapter (PIA) registers:
  * `$D010`: Keyboard input data register.
  * `$D011`: Keyboard control register (bit 7 strobe indicates key pressed).
  * `$D012`: Display output data register.
  * `$D013`: Display control register (bit 7 indicates display ready).
* **`$E000 - $EFFF`**: Apple-1 BASIC ROM preset area.
* **`$F000 - $FDFF`**: Krusader Assembler ROM preset area.
* **`$FF00 - $FFFF`**: Woz Monitor ROM.

---

*Manual version: 1.1*  
*Created: June 2026*
