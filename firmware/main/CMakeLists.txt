idf_component_register(
	SRCS "index.cpp" "scan.cpp" "display.cpp" "font/index.cpp" "font/mono-40.cpp"
	INCLUDE_DIRS "."

	PRIV_REQUIRES spi_flash
	PRIV_REQUIRES esp_driver_gpio
	PRIV_REQUIRES usb
)
