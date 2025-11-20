idf_component_register(
	SRCS "index.c"
	SRCS "scan.c"

	PRIV_REQUIRES spi_flash
	INCLUDE_DIRS ""

	PRIV_REQUIRES esp_driver_gpio
	PRIV_REQUIRES usb
)
