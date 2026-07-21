# Host-side regression test runner for the ActiveRFID -> RT1062 port.
#
# These test the PURE LOGIC modules (protocol parsing, CRC, calendar
# math, record builders, OTA download/install logic, etc) on your PC
# with plain gcc -- nothing here needs the target board, NXP SDK,
# lwIP, or littlefs. Run this after ANY edit to firmware_source/ to
# catch regressions before you even touch hardware.
#
# Usage:
#   make            # build and run every test, print a pass/fail summary
#   make clean      # remove built test binaries

.PHONY: all test clean
all: test

SRC := ../firmware_source
CC := gcc
CFLAGS := -std=c11 -Wall -Wextra -I$(SRC) -Itest_stubs
LDLIBS := -lm

TESTS := \
  test_rfid_logic \
  test_nrf_spi_protocol \
  test_pc_protocol \
  test_tcp_session \
  test_udp_discovery \
  test_civil_time \
  test_rtc_time \
  test_time_sync \
  test_nand_log_logic \
  test_uhf_crc \
  test_uhf_chip_array \
  test_uhf_packet_parser \
  test_uhf_commands \
  test_uhf_reader \
  test_gprs_records \
  test_gprs_config_record \
  test_gprs_response_parser \
  test_gprs_modem \
  test_gprs_batch_sender \
  test_fw_version_check \
  test_crc32 \
  test_http_response_parser \
  test_fw_downloader \
  test_fw_install_mcuboot \
  test_ms_time \
  test_settings_store \
  test_ip_addr_parse \
  test_genie_protocol \
  test_genie_display \
  test_neo_m8t_protocol \
  test_neo_m8t_reader \
  test_finish_lynx_protocol

test_rfid_logic_DEPS          := rfid_logic.c
test_nrf_spi_protocol_DEPS    := nrf_spi_protocol.c
test_nrf_spi_protocol_EXTRA   := test_stubs/debug_console_stub.c
test_pc_protocol_DEPS         := pc_protocol.c
test_tcp_session_DEPS         := pc_protocol.c tcp_session.c
test_tcp_session_EXTRA        := test_stubs/debug_console_stub.c
test_udp_discovery_DEPS       := pc_protocol.c udp_discovery.c
test_civil_time_DEPS          := civil_time.c
test_rtc_time_DEPS            := civil_time.c rtc_time.c
test_time_sync_DEPS           := civil_time.c rtc_time.c time_sync.c
test_nand_log_logic_DEPS      := nand_log_logic.c
test_uhf_crc_DEPS             := uhf_crc.c
test_uhf_chip_array_DEPS      := uhf_chip_array.c
test_uhf_packet_parser_DEPS   := uhf_crc.c uhf_packet_parser.c
test_uhf_commands_DEPS        := uhf_crc.c uhf_commands.c
test_uhf_reader_DEPS          := uhf_crc.c uhf_commands.c uhf_packet_parser.c uhf_reader.c
test_uhf_reader_EXTRA         := test_stubs/debug_console_stub.c
test_gprs_records_DEPS        := gprs_records.c
test_gprs_config_record_DEPS  := gprs_config_record.c
test_gprs_response_parser_DEPS:= gprs_response_parser.c
test_gprs_modem_DEPS          := gprs_response_parser.c gprs_modem.c
test_gprs_batch_sender_DEPS   := gprs_records.c gprs_batch_sender.c
test_fw_version_check_DEPS    := fw_version_check.c
test_crc32_DEPS                := crc32.c
test_http_response_parser_DEPS := http_response_parser.c
test_fw_downloader_DEPS        := http_response_parser.c crc32.c fw_downloader.c
test_fw_install_mcuboot_DEPS   := fw_install_mcuboot.c
test_ms_time_DEPS               := ms_time.c
test_settings_store_DEPS        := settings_store.c crc32.c
test_ip_addr_parse_DEPS         := ip_addr_parse.c
test_genie_protocol_DEPS        := genie_protocol.c
test_genie_display_DEPS         := genie_protocol.c genie_display.c ms_time.c
test_neo_m8t_protocol_DEPS       := neo_m8t_protocol.c
test_neo_m8t_reader_DEPS         := neo_m8t_protocol.c neo_m8t_reader.c ms_time.c civil_time.c rtc_time.c
test_neo_m8t_reader_EXTRA        := test_stubs/debug_console_stub.c
test_finish_lynx_protocol_DEPS   := finish_lynx_protocol.c civil_time.c

test: $(TESTS)
	@echo ""
	@echo "=== Running all tests ==="
	@pass=0; fail=0; \
	for t in $(TESTS); do \
		if ./$$t > /tmp/$$t.log 2>&1; then \
			echo "PASS: $$t"; pass=$$((pass+1)); \
		else \
			echo "FAIL: $$t (see /tmp/$$t.log)"; fail=$$((fail+1)); \
		fi; \
	done; \
	echo ""; \
	echo "$$pass passed, $$fail failed"; \
	test $$fail -eq 0

define BUILD_RULE
$(1): $(1).c $$(addprefix $$(SRC)/,$$($(1)_DEPS)) $$($(1)_EXTRA)
	$$(CC) $$(CFLAGS) -o $$@ $$^ $$(LDLIBS)
endef
$(foreach t,$(TESTS),$(eval $(call BUILD_RULE,$(t))))

clean:
	rm -f $(TESTS)
