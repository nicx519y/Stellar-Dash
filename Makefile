all: application bootloader

application: FORCE
	@echo "Building application..."
	$(MAKE) -C application -j16

bootloader: FORCE
	@echo "Building bootloader..."
	$(MAKE) -C bootloader -j16

flash-application: FORCE
	@echo "Flashing application..."
	$(MAKE) -C application flash

flash-web-resources: FORCE
	@echo "Flashing web resources..."
	$(MAKE) -C application flash-web-resources

flash-bootloader: FORCE
	@echo "Flashing bootloader..."
	$(MAKE) -C bootloader flash

clean:
	@echo "Cleaning application..."
	$(MAKE) -C application clean
	@echo "Cleaning bootloader..."
	$(MAKE) -C bootloader clean

FORCE:
