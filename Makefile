AS=sh3eb-elf-as
CC=sh3eb-elf-gcc
CPP=sh3eb-elf-g++
OBJCOPY=sh3eb-elf-objcopy
MKG3A=mkg3a
RM=rm
CFLAGS=-m4a-nofpu -mb -Isrc -O2 -fmerge-all-constants -ggdb -mhitachi -fshort-enums -flto -Wall -Wextra -I../../include -lgcc -L../../lib -I../zlib-1.2.8/ -I../libpng-1.6.10/
LDFLAGS=$(CFLAGS) -nostartfiles -T../../toolchain/prizm.x -Wl,-static -Wl,-gc-sections -lgcc -flto -lz -lpng16 -L../libpng-1.6.10/.libs/ -L../zlib-1.2.8/
OBJECTS=main.o filegui.o fileicons.o
PROJ_NAME=ImageViewer
BIN=$(PROJ_NAME).bin
ELF=$(PROJ_NAME).elf
ADDIN=$(BIN:.bin=.g3a)
 
all: $(ADDIN)


$(ADDIN): $(BIN)
	$(MKG3A) -n :ImageViewer -i uns:unselected.bmp -i sel:selected.bmp $< $@
 
.s.o:
	$(AS) -c $< -o $@
 .S.o:
	$(AS) -c $< -o $@
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
	rm -f $(OBJECTS) $(PROJ_NAME).bin $(PROJ_NAME).elf $(ADDIN)
