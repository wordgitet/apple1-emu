$! BUILD.COM - Build Apple-1 Emulator on OpenVMS
$!
$! Auto-set default directory to the script's location
$ this_dir = f$parse(f$environment("procedure"),,,"device") + f$parse(f$environment("procedure"),,,"directory")
$ set default 'this_dir'
$!
$ write sys$output "Compiling Apple-1 Emulator..."
$ CC/DEFINE=(APPLE1_PORT_VMS,APPLE1_TERM_DUMB,APPLE1_OMIT_CHARMAP) main.c
$ CC/DEFINE=(APPLE1_PORT_VMS,APPLE1_TERM_DUMB,APPLE1_OMIT_CHARMAP) cpu.c
$ CC/DEFINE=(APPLE1_PORT_VMS,APPLE1_TERM_DUMB,APPLE1_OMIT_CHARMAP) bus.c
$ CC/DEFINE=(APPLE1_PORT_VMS,APPLE1_TERM_DUMB,APPLE1_OMIT_CHARMAP) io.c
$ CC/DEFINE=(APPLE1_PORT_VMS,APPLE1_TERM_DUMB,APPLE1_OMIT_CHARMAP) aci.c
$ CC/DEFINE=(APPLE1_PORT_VMS,APPLE1_TERM_DUMB,APPLE1_OMIT_CHARMAP) krusader.c
$ CC/DEFINE=(APPLE1_PORT_VMS,APPLE1_TERM_DUMB,APPLE1_OMIT_CHARMAP) disasm.c
$ CC/DEFINE=(APPLE1_PORT_VMS,APPLE1_TERM_DUMB,APPLE1_OMIT_CHARMAP) dbg.c
$ CC/DEFINE=(APPLE1_PORT_VMS,APPLE1_TERM_DUMB,APPLE1_OMIT_CHARMAP) term.c
$ CC/DEFINE=(APPLE1_PORT_VMS,APPLE1_TERM_DUMB,APPLE1_OMIT_CHARMAP) port.c
$ CC/DEFINE=(APPLE1_PORT_VMS,APPLE1_TERM_DUMB,APPLE1_OMIT_CHARMAP) cli_config.c
$!
$ write sys$output "Linking..."
$ LINK/EXECUTABLE=apple1.exe main.obj,cpu.obj,bus.obj,io.obj,aci.obj,krusader.obj,disasm.obj,dbg.obj,term.obj,port.obj,cli_config.obj
$!
$ write sys$output "Build Complete!"
