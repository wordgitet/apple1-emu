</$objtype/mkfile

TARG=out

OFILES=\
	main.$O\
	cli_config.$O\
	cpu.$O\
	bus.$O\
	io.$O\
	aci.$O\
	krusader.$O\
	disasm.$O\
	dbg.$O\
	term.$O\
	port.$O\

HFILES=\
	port.h\
	port_stdarg.h\
	port_stdarg_plan9.h\
	port_host.h\
	apple1limit_host.h\
	bus.h\
	cpu.h\
	dbg.h\
	disasm.h\
	aci.h\
	krusader.h\
	io.h\
	cli_config.h\
	term_apple1.h\
	apple1limit.h\
	embedded_roms.h\

CFLAGS=-DAPPLE1_OMIT_CHARMAP -DAPPLE1_PORT_PLAN9 -DAPPLE1_TERM_VT100

</sys/src/cmd/mkone

# Extra prerequisites (must come after mkone — otherwise plain "mk" only
# rebuilds port.$O, not the full $O.out binary).
port.$O: port.c port_string.c port_plan9.c
term.$O: term.c term_vt100.c

# mk links $O.out (6.out on amd64).  dircp may leave a Linux ELF apple1
# in the tree — rm -f apple1.  ./6.out -H  or  mk apple1

apple1:V: $O.$TARG
	cp $O.$TARG apple1

pcc:V:
	mk clean
	CC=pcc LD=pcc LIB=-lape CFLAGS='-DAPPLE1_OMIT_CHARMAP -DAPPLE1_PORT_PLAN9 -DAPPLE1_TERM_VT100 -DAPPLE1_PORT_PLAN9_APE' mk all

install:V: all
	cp $O.$TARG /$objtype/bin/apple1

clean:V:
	rm -f *.$O $O.$TARG apple1 apple1.exe
