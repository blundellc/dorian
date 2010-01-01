cc=gcc
cflags="-O2 -Wall -pedantic -D_FORTIFY_SOURCE=2 -fstack-protector -fPIE -c -std=c99"
ld=gcc
ldflags="-s -pie -z relro -fstack-protector"

all:V: grey list cleanup spf

clean:V:
    rm -f *.o grey list cleanup spf

%.o: %.c common.c mkfile
	$cc $cflags -o $stem.o $stem.c

grey: grey.o mkfile
	$ld $ldflags -o grey grey.o -lgdbm

list: list.o mkfile
	$ld $ldflags -o list list.o -lgdbm

cleanup: cleanup.o mkfile
	$ld $ldflags -o cleanup cleanup.o -lgdbm

spf: spf.o mkfile
	$ld $ldflags -o spf spf.o -lspf2

install:V:
    install -d -m 755 $DESTDIR/usr/bin
    install -m 511 grey     $DESTDIR/usr/bin/dorian-grey
    install -m 511 list     $DESTDIR/usr/bin/dorian-list
    install -m 511 cleanup  $DESTDIR/usr/bin/dorian-cleanup
    install -m 511 spf      $DESTDIR/usr/bin/dorian-spf
