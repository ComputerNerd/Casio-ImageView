CC=sh3eb-elf-gcc
CPP=sh3eb-elf-g++
OBJCOPY=sh3eb-elf-objcopy
MKG3A=mkg3a
RM=rm
CFLAGS=-m4a-nofpu -mb -Isrc -O2 -fmerge-all-constants -ggdb -mhitachi -flto -fuse-linker-plugin -Wall -Wextra -I../../include -lgcc -L../../lib -I../zlib-1.2.8/ -I../libpng-1.6.20/
LDFLAGS=$(CFLAGS) -nostartfiles -T../../toolchain/prizm.x -Wl,-static -Wl,-gc-sections -lgcc -lpng16 -lz -L../libpng-1.6.20/.libs/ -L../zlib-1.2.8/
OBJECTS=main.o filegui.o fileicons.o
PROJ_NAME=PNGviewer
BIN=$(PROJ_NAME).bin
ELF=$(PROJ_NAME).elf
ADDIN=$(BIN:.bin=.g3a)
 
all: $(ADDIN)

$(ADDIN): $(BIN)
	$(MKG3A) -n :"PNG viewer" -i uns:unselected.png -i sel:selected.png $< $@
 
.c.o:
	$(CC) -c $(CFLAGS) $< -o $@
	
.cpp.o:
	$(CC) -c $(CFLAGS) $< -o $@
	
.cc.o:
	$(CC) -c $(CFLAGS) $< -o $@

$(ELF): $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@
$(BIN): $(ELF)
	$(OBJCOPY) -O binary $(PROJ_NAME).elf $(BIN)

clean:
	rm -f $(OBJECTS) $(BIN) $(ELF) $(ADDIN)
