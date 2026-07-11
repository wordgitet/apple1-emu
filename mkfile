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

BARECFLAGS=$CFLAGS\
	-DAPPLE1_OMIT_DEBUGGER\
	-DAPPLE1_OMIT_ACI\
	-DAPPLE1_OMIT_KRUSADER\
	-DAPPLE1_OMIT_PASTE\
	-DAPPLE1_OMIT_PIA_THROTTLE\
	-DAPPLE1_OMIT_KBD_BOUNCE\
	-DAPPLE1_OMIT_DISKIO\
	-DAPPLE1_OMIT_BUS_ACCESS_CB\
	-DAPPLE1_ZERO_MALLOC

BAREOFILES=\
	bare_main.$O\
	bare_cpu.$O\
	bare_bus.$O\
	bare_io.$O\
	bare_term.$O\
	bare_port.$O\

</sys/src/cmd/mkone

# Extra prerequisites (must come after mkone — otherwise plain "mk" only
# rebuilds port.$O, not the full $O.out binary).
port.$O: port.c port_string.c port_plan9.c
term.$O: term.c term_vt100.c

# mk links $O.out (6.out on amd64).  dircp may leave a Linux ELF apple1
# in the tree — rm -f apple1.  ./6.out -H  or  mk apple1

apple1:V: $O.$TARG
	cp $O.$TARG apple1

# Minimal build: same omit flags as make bare (GNU Makefile).
apple1_bare:V: $BAREOFILES
	$LD -o apple1_bare $BAREOFILES

bare:V: apple1_bare

bare_main.$O: main.c $HFILES
	$CC $BARECFLAGS main.c
	mv main.$O $@

bare_cpu.$O: cpu.c $HFILES
	$CC $BARECFLAGS cpu.c
	mv cpu.$O $@

bare_bus.$O: bus.c $HFILES
	$CC $BARECFLAGS bus.c
	mv bus.$O $@

bare_io.$O: io.c $HFILES
	$CC $BARECFLAGS io.c
	mv io.$O $@

bare_term.$O: term.c term_vt100.c $HFILES
	$CC $BARECFLAGS term.c
	mv term.$O $@

bare_port.$O: port.c port_string.c port_plan9.c $HFILES
	$CC $BARECFLAGS port.c
	mv port.$O $@

# Single-file amalgamation (no Python): rc + awk, then 6c/6l.
amal:V: apple1.c apple1.h
	@{echo amalgamation ready: apple1.c apple1.h}

apple1.c apple1.h:V: tools/amalgamate.rc tools/amalgamate_plan9.awk
	rc tools/amalgamate.rc

amal-single:V: apple1.c apple1.h
	6c $CFLAGS apple1.c
	6l -o $O.$TARG apple1.$O
	cp $O.$TARG apple1



install:V: all
	cp $O.$TARG /$objtype/bin/apple1

clean:V:
	rm -f *.$O $O.$TARG apple1 apple1_bare apple1.exe bare_*.$O
