# Makefile

FED_BASE = fed-gen/NrfDistribute/src-gen
FED_C    = $(FED_BASE)/federate__c
FED_P    = $(FED_BASE)/federate__p

BOARD = nrf52840dk_nrf52840

.PHONY: build lfc west clean

# High-level build target (runs everything)
build: lfc west
	@echo "Full build complete."

# LFC compilation
lfc:
	@echo "Running LFC..."
	lfc src/NrfDistribute.lf

# West build only (no LFC)
west:
	@echo "Building federates with west..."

	@echo " → federate__c"
	west build -p -b $(BOARD) \
		-s $(FED_C) \
		-d $(FED_C)/build

	@echo " → federate__p"
	west build -p -b $(BOARD) \
		-s $(FED_P) \
		-d $(FED_P)/build

	@echo "West build complete."

# Clean
clean:
	@echo "Cleaning up..."
	rm -rf build/
	rm -rf fed-gen/
