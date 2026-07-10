/*
 * fw_downloader.h
 *
 * Orchestrates an HTTP GET of a firmware binary using
 * http_response_parser.h + a transport interface, streaming the body
 * to a caller-supplied sink while computing a running CRC32
 * (crc32.h). Was the download-loop shape of install_firmware()
 * (buDownloadInit/buDownloadTick), reimplemented from scratch since
 * board_update.lib's actual internals weren't available to port --
 * see the conversation notes on why the flash-install step itself is
 * deliberately NOT part of this file.
 *
 * The sink is intentionally generic (a byte-receiving callback) so
 * this module has no opinion on where the bytes ultimately land --
 * a RAM buffer, a littlefs staging file, or eventually a flash bank/
 * MCUboot slot, once that architecture decision is made. This mirrors
 * the byte-sink pattern used elsewhere in this port (gprs_batch_sender.h).
 */

#ifndef FW_DOWNLOADER_H
#define FW_DOWNLOADER_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FW_DL_OK = 0,
    FW_DL_CONNECT_FAILED,
    FW_DL_HTTP_ERROR,
    FW_DL_PARSE_ERROR,
    FW_DL_SINK_ERROR,
    FW_DL_TIMEOUT,
} fw_download_result_t;

typedef struct {
    long total_bytes;
    long bytes_received;
    uint32_t crc32;
} fw_download_progress_t;

typedef struct {
    void *ctx;
    int (*connect)(void *ctx, const char *host, uint16_t port, uint32_t timeout_ms);
    int (*send)(void *ctx, const uint8_t *buf, size_t len);
    int (*recv)(void *ctx, uint8_t *buf, size_t max_len, uint32_t timeout_ms);
    void (*close)(void *ctx);
} fw_http_transport_t;

typedef int (*fw_download_sink_fn)(void *ctx, const uint8_t *data, size_t len);

fw_download_result_t fw_download_firmware(const fw_http_transport_t *transport,
                                           const char *host, uint16_t port, const char *path,
                                           fw_download_sink_fn sink, void *sink_ctx,
                                           uint32_t connect_timeout_ms, uint32_t io_timeout_ms,
                                           fw_download_progress_t *out_progress);

#ifdef __cplusplus
}
#endif

#endif /* FW_DOWNLOADER_H */
