idf_component_register(
	SRCS "index.cpp"
	SRCS "beep.cpp"

	PRIV_REQUIRES spi_flash
	INCLUDE_DIRS ""

	REQUIRES esp_driver_gpio
	REQUIRES esp_driver_i2s
)
