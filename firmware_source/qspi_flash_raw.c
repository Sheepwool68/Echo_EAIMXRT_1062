/*
 * qspi_flash_raw.c
 *
 * See qspi_flash_raw.h header comment. Same SCAFFOLD tier as
 * nand_log_flash_qspi.c originally was -- this file contains exactly
 * the same logic that used to live there as static functions, now
 * public and taking absolute flash addresses instead of an implicit
 * partition offset, so both littlefs storage and MCUboot slot writes
 * can share it.
 */

#include "qspi_flash_raw.h"
#include "fsl_flexspi.h"

#define FLEXSPI_INSTANCE FLEXSPI

/* FlexSPI LUT sequence indices -- see nand_log_flash_qspi.c's original
 * notes (unchanged from there): TODO build via FLEXSPI_UpdateLUT(),
 * reuse your existing boot LUT's read sequence if XIP-booting from
 * this chip, opcodes are standard IS25WP128 JEDEC commands. */
#define LUT_SEQ_READ         0
#define LUT_SEQ_WRITE_ENABLE 1
#define LUT_SEQ_READ_STATUS  2
#define LUT_SEQ_PAGE_PROGRAM 3
#define LUT_SEQ_SECTOR_ERASE 4

int qspi_flash_raw_init(void)
{
    /* TODO: FLEXSPI_Init() + FLEXSPI_UpdateLUT() -- see header note.
     * Kept as a stub rather than silently assumed, matching the
     * original file's TODO at nand_log_flash_qspi_get_config(). */
    return 0;
}

int qspi_flash_read(uint32_t addr, uint8_t *out, size_t len)
{
    const uint8_t *src = (const uint8_t *)(uintptr_t)(QSPI_FLASH_AMBA_BASE + addr);
    size_t i;
    for (i = 0; i < len; i++) {
        out[i] = src[i];
    }
    return 0;
}

static int flash_wait_ready(void)
{
    flexspi_transfer_t xfer = {0};
    uint32_t status;
    int max_polls = 100000;

    xfer.deviceAddress = 0;
    xfer.port = kFLEXSPI_PortA1; /* TODO: confirm port assignment */
    xfer.cmdType = kFLEXSPI_Read;
    xfer.SeqNumber = 1;
    xfer.seqIndex = LUT_SEQ_READ_STATUS;
    xfer.data = &status;
    xfer.dataSize = 1;

    while (max_polls-- > 0) {
        if (FLEXSPI_TransferBlocking(FLEXSPI_INSTANCE, &xfer) != kStatus_Success) {
            return -1;
        }
        if ((status & 0x01u) == 0) {
            return 0;
        }
    }
    return -1;
}

static int flash_write_enable(void)
{
    flexspi_transfer_t xfer = {0};
    xfer.deviceAddress = 0;
    xfer.port = kFLEXSPI_PortA1;
    xfer.cmdType = kFLEXSPI_Command;
    xfer.SeqNumber = 1;
    xfer.seqIndex = LUT_SEQ_WRITE_ENABLE;
    return (FLEXSPI_TransferBlocking(FLEXSPI_INSTANCE, &xfer) == kStatus_Success) ? 0 : -1;
}

int qspi_flash_program(uint32_t addr, const uint8_t *data, size_t len)
{
    size_t remaining = len;
    const uint8_t *src = data;

    while (remaining > 0) {
        uint32_t page_offset = addr % QSPI_FLASH_PAGE_SIZE;
        uint32_t chunk = QSPI_FLASH_PAGE_SIZE - page_offset;
        if (chunk > remaining) {
            chunk = (uint32_t)remaining;
        }

        if (flash_write_enable() != 0) {
            return -1;
        }

        {
            flexspi_transfer_t xfer = {0};
            xfer.deviceAddress = addr;
            xfer.port = kFLEXSPI_PortA1;
            xfer.cmdType = kFLEXSPI_Write;
            xfer.SeqNumber = 1;
            xfer.seqIndex = LUT_SEQ_PAGE_PROGRAM;
            xfer.data = (uint32_t *)(uintptr_t)src;
            xfer.dataSize = chunk;
            if (FLEXSPI_TransferBlocking(FLEXSPI_INSTANCE, &xfer) != kStatus_Success) {
                return -1;
            }
        }

        if (flash_wait_ready() != 0) {
            return -1;
        }

        addr += chunk;
        src += chunk;
        remaining -= chunk;
    }

    FLEXSPI_SoftwareReset(FLEXSPI_INSTANCE); /* invalidate AMBA read cache */
    return 0;
}

int qspi_flash_erase_sector(uint32_t addr)
{
    flexspi_transfer_t xfer = {0};
    uint32_t sector_addr = (addr / QSPI_FLASH_SECTOR_SIZE) * QSPI_FLASH_SECTOR_SIZE;

    if (flash_write_enable() != 0) {
        return -1;
    }

    xfer.deviceAddress = sector_addr;
    xfer.port = kFLEXSPI_PortA1;
    xfer.cmdType = kFLEXSPI_Command;
    xfer.SeqNumber = 1;
    xfer.seqIndex = LUT_SEQ_SECTOR_ERASE;
    if (FLEXSPI_TransferBlocking(FLEXSPI_INSTANCE, &xfer) != kStatus_Success) {
        return -1;
    }

    if (flash_wait_ready() != 0) {
        return -1;
    }

    FLEXSPI_SoftwareReset(FLEXSPI_INSTANCE);
    return 0;
}
