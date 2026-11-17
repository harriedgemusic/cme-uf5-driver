CC         = clang
SWIFTC     = xcrun swiftc
CFLAGS     = -Wall -Wextra -Wno-unused-parameter -O2
ARCHS      = -arch arm64 -arch x86_64
FRAMEWORKS = -framework CoreFoundation -framework CoreMIDI -framework IOKit

BUILD_DIR    = build
DIST_DIR     = dist
PLUGIN_NAME  = CMEUFMIDIDriver.plugin
PLUGIN_DIR   = $(BUILD_DIR)/$(PLUGIN_NAME)
PLUGIN_BIN   = $(PLUGIN_DIR)/Contents/MacOS/CMEUFMIDIDriver
CTL_BIN      = $(BUILD_DIR)/cme-uf5-ctl

APP_NAME     = CME UF5 Control.app
APP_DIR      = $(BUILD_DIR)/$(APP_NAME)
APP_STAGE    = $(BUILD_DIR)/CMEUF5ControlBin
APP_ICNS     = $(BUILD_DIR)/AppIcon.icns

PKG_FILE     = $(DIST_DIR)/CME-UF5-macOS-Installer.pkg

USER_MIDI_DRIVERS_DIR = $(HOME)/Library/Audio/MIDI Drivers
USER_LOGIC_CME_DIR    = $(HOME)/Music/Audio Music Apps/MIDI Device Scripts/CME
USER_LOGIC_CMC_DIR    = $(HOME)/Music/Audio Music Apps/MIDI Device Scripts/Central Music Co

CODESIGN_ID := $(shell security find-identity -v -p codesigning 2>/dev/null | awk -F'"' '/Apple Development|Developer ID Application/ {print $$2; exit}')
ifeq ($(CODESIGN_ID),)
CODESIGN_ID := -
endif

.PHONY: all plugin ctl icons app sign pkg install uninstall reload status monitor clean

all: plugin ctl app sign pkg

plugin: $(PLUGIN_BIN)

$(PLUGIN_BIN): src/CMEUFMIDIDriver.c Info.plist
	@mkdir -p "$(PLUGIN_DIR)/Contents/MacOS"
	@cp Info.plist "$(PLUGIN_DIR)/Contents/Info.plist"
	$(CC) $(CFLAGS) $(ARCHS) -bundle src/CMEUFMIDIDriver.c $(FRAMEWORKS) -o "$(PLUGIN_BIN)"

ctl: $(CTL_BIN)

$(CTL_BIN): src/cme_uf5_ctl.c
	@mkdir -p "$(BUILD_DIR)"
	$(CC) $(CFLAGS) $(ARCHS) src/cme_uf5_ctl.c $(FRAMEWORKS) -o "$(CTL_BIN)"

icons: $(APP_ICNS)

$(APP_ICNS): scripts/generate_icons.swift
	@mkdir -p "$(BUILD_DIR)" "logic/UF5.device"
	swift scripts/generate_icons.swift
	iconutil -c icns "$(BUILD_DIR)/AppIcon.iconset" -o "$(APP_ICNS)"

app: $(APP_STAGE)

$(APP_STAGE): app/CMEUF5Control.swift app/Info.plist $(APP_ICNS)
	@mkdir -p "$(APP_DIR)/Contents/MacOS" "$(APP_DIR)/Contents/Resources"
	@cp app/Info.plist "$(APP_DIR)/Contents/Info.plist"
	@cp "$(APP_ICNS)" "$(APP_DIR)/Contents/Resources/AppIcon.icns"
	$(SWIFTC) -parse-as-library -O -target arm64-apple-macos13.0 app/CMEUF5Control.swift -framework SwiftUI -framework AppKit -framework CoreMIDI -framework IOKit -o "$(BUILD_DIR)/CMEUF5Control_arm64"
	$(SWIFTC) -parse-as-library -O -target x86_64-apple-macos13.0 app/CMEUF5Control.swift -framework SwiftUI -framework AppKit -framework CoreMIDI -framework IOKit -o "$(BUILD_DIR)/CMEUF5Control_x86_64"
	lipo -create -output "$(APP_STAGE)" "$(BUILD_DIR)/CMEUF5Control_arm64" "$(BUILD_DIR)/CMEUF5Control_x86_64"
	@cp "$(APP_STAGE)" "$(APP_DIR)/Contents/MacOS/CMEUF5Control"
	@rm -f "$(BUILD_DIR)/CMEUF5Control_arm64" "$(BUILD_DIR)/CMEUF5Control_x86_64"

sign: $(PLUGIN_BIN) $(CTL_BIN) $(APP_STAGE)
	codesign -f -s "$(CODESIGN_ID)" "$(PLUGIN_DIR)"
	codesign -f -s "$(CODESIGN_ID)" "$(CTL_BIN)"
	codesign -f --deep -s "$(CODESIGN_ID)" "$(APP_DIR)"

pkg: sign logic/UF5.device/config.lua
	@chmod +x scripts/build_pkg.sh installer/scripts/postinstall
	@./scripts/build_pkg.sh

install: all
	@mkdir -p "$(USER_MIDI_DRIVERS_DIR)"
	@rm -rf "$(USER_MIDI_DRIVERS_DIR)/$(PLUGIN_NAME)"
	@cp -R "$(PLUGIN_DIR)" "$(USER_MIDI_DRIVERS_DIR)/"
	@echo "Installed CoreMIDI Driver -> $(USER_MIDI_DRIVERS_DIR)/$(PLUGIN_NAME)"
	@mkdir -p "$(USER_LOGIC_CME_DIR)" "$(USER_LOGIC_CMC_DIR)"
	@rm -rf "$(USER_LOGIC_CME_DIR)/UF5.device" "$(USER_LOGIC_CME_DIR)/CME UF5.device" "$(USER_LOGIC_CMC_DIR)/CME UF MIDI Device.device"
	@cp -R "logic/UF5.device" "$(USER_LOGIC_CME_DIR)/UF5.device"
	@cp -R "logic/UF5.device" "$(USER_LOGIC_CME_DIR)/CME UF5.device"
	@cp -R "logic/UF5.device" "$(USER_LOGIC_CMC_DIR)/CME UF MIDI Device.device"
	@echo "Installed Logic Pro Control Surface -> $(USER_LOGIC_CME_DIR)/UF5.device"
	@if [ -w "/Applications" ]; then \
		rm -rf "/Applications/$(APP_NAME)"; \
		cp -R "$(APP_DIR)" "/Applications/$(APP_NAME)"; \
		echo "Installed Control App -> /Applications/$(APP_NAME)"; \
	else \
		mkdir -p "$(HOME)/Applications"; \
		rm -rf "$(HOME)/Applications/$(APP_NAME)"; \
		cp -R "$(APP_DIR)" "$(HOME)/Applications/$(APP_NAME)"; \
		echo "Installed Control App -> $(HOME)/Applications/$(APP_NAME)"; \
	fi
	@"$(CTL_BIN)" reload

uninstall:
	@rm -rf "$(USER_MIDI_DRIVERS_DIR)/$(PLUGIN_NAME)"
	@rm -rf "$(USER_LOGIC_CME_DIR)/UF5.device" "$(USER_LOGIC_CME_DIR)/CME UF5.device"
	@rm -rf "/Applications/$(APP_NAME)" "$(HOME)/Applications/$(APP_NAME)"
	@killall MIDIServer 2>/dev/null || true
	@echo "Uninstalled CME UF5 driver, Control App, and Logic Pro Control Surface."

reload: $(CTL_BIN)
	@"$(CTL_BIN)" reload

status: $(CTL_BIN)
	@"$(CTL_BIN)" status

monitor: $(CTL_BIN)
	@"$(CTL_BIN)" monitor

clean:
	rm -rf "$(BUILD_DIR)" "$(DIST_DIR)"
