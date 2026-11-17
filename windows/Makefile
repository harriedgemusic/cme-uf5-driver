CC      = x86_64-w64-mingw32-gcc
CXX     = x86_64-w64-mingw32-g++
WINDRES = x86_64-w64-mingw32-windres

CFLAGS   = -Wall -Wextra -O2 -DWIN32_LEAN_AND_MEAN -DUNICODE -D_UNICODE
CXXFLAGS = -Wall -Wextra -O2 -std=c++17 -DWIN32_LEAN_AND_MEAN -DUNICODE -D_UNICODE
LDFLAGS  = -s -static-libgcc -static-libstdc++
LIBS     = -lwinusb -lsetupapi -lwinmm -lgdiplus -lgdi32 -luser32 -lcomctl32 -lshell32 -ladvapi32

BIN_DIR = bin
SRC_DIR = src

TARGET_GUI    = $(BIN_DIR)/CMEUF5Control.exe
TARGET_BRIDGE = $(BIN_DIR)/CMEUF5Bridge.exe
TARGET_CTL    = $(BIN_DIR)/cme-uf5-ctl.exe

.PHONY: all clean

all: $(BIN_DIR) $(TARGET_CTL) $(TARGET_BRIDGE) $(TARGET_GUI)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

# CLI Tool
$(TARGET_CTL): $(SRC_DIR)/cme_uf5_ctl.c $(SRC_DIR)/cme_protocol.c $(SRC_DIR)/cme_winusb.c $(SRC_DIR)/cme_virtual_midi.c
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@ $(LIBS)

# Service / Background Bridge
$(TARGET_BRIDGE): $(SRC_DIR)/cme_service.c $(SRC_DIR)/cme_protocol.c $(SRC_DIR)/cme_winusb.c $(SRC_DIR)/cme_virtual_midi.c
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@ $(LIBS)

# GUI Control Center
$(TARGET_GUI): $(SRC_DIR)/cme_gui_app.cpp $(SRC_DIR)/cme_protocol.c $(SRC_DIR)/cme_winusb.c $(SRC_DIR)/cme_virtual_midi.c $(SRC_DIR)/resource.o
	$(CXX) $(CXXFLAGS) -mwindows $(LDFLAGS) $^ -o $@ $(LIBS)

$(SRC_DIR)/resource.o: $(SRC_DIR)/resource.rc $(SRC_DIR)/app.manifest
	cd $(SRC_DIR) && $(WINDRES) resource.rc -o resource.o

clean:
	rm -rf $(BIN_DIR) $(SRC_DIR)/*.o

