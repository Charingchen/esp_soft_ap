deps_config := \
	/home/esp/esp-idf/components/app_trace/Kconfig \
	/home/esp/esp-idf/components/aws_iot/Kconfig \
	/home/esp/esp-idf/components/bt/Kconfig \
	/home/esp/esp-idf/components/driver/Kconfig \
	/home/esp/esp-idf/components/esp32/Kconfig \
	/home/esp/esp-idf/components/esp_adc_cal/Kconfig \
	/home/esp/esp-idf/components/ethernet/Kconfig \
	/home/esp/esp-idf/components/fatfs/Kconfig \
	/home/esp/esp-idf/components/freertos/Kconfig \
	/home/esp/esp-idf/components/heap/Kconfig \
	/home/esp/esp-idf/components/libsodium/Kconfig \
	/home/esp/esp-idf/components/log/Kconfig \
	/home/esp/esp-idf/components/lwip/Kconfig \
	/home/esp/esp-idf/components/mbedtls/Kconfig \
	/home/esp/esp-idf/components/openssl/Kconfig \
	/home/esp/esp-idf/components/pthread/Kconfig \
	/home/esp/esp-idf/components/spi_flash/Kconfig \
	/home/esp/esp-idf/components/spiffs/Kconfig \
	/home/esp/esp-idf/components/tcpip_adapter/Kconfig \
	/home/esp/esp-idf/components/wear_levelling/Kconfig \
	/home/esp/esp-idf/components/bootloader/Kconfig.projbuild \
	/home/esp/esp-idf/components/esptool_py/Kconfig.projbuild \
	/home/esp/esp-idf/components/partition_table/Kconfig.projbuild \
	/home/esp/esp-idf/Kconfig

include/config/auto.conf: \
	$(deps_config)


$(deps_config): ;
