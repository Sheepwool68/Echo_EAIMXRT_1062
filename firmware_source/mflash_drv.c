/*
 * mflash_drv.c
 *
 * Adapted from the Embedded Artists SDK's
 * components/flash/mflash/evkcmimxrt1060/mflash_drv.c (confirmed real,
 * from the SDK actually installed for this board -- see mflash_drv.h
 * for why this variant, not the generic mimxrt1062 one).
 *
 * TWO CHANGES from the reference:
 *
 * 1. flexspiRootClk corrected from the reference's assumed 133000000 to
 *    130909091 (1440000000/11), CONFIRMED from this project's own
 *    board/clock_config.c (FLEXSPI_CLK_ROOT.outFreq = 1440/11 MHz) --
 *    close to but not exactly the reference's assumption, and this
 *    session's established discipline is to use the real confirmed
 *    value, not an inherited example default (see project memory on the
 *    GPRS/UHF UART clock bugs found the same way).
 *
 * 2. RAM-residency: every function in the erase/program/read call path
 *    (and FLEXSPI_Init/GetDefaultConfig/SetFlashConfig/UpdateLUT, called
 *    from mflash_drv_init_internal()) is decorated with
 *    AT_QUICKACCESS_SECTION_CODE, matching the reference's own explicit
 *    requirement ("necessary to place at least mflash_drv.o,
 *    fsl_flexspi.o to SRAM" -- see that comment in
 *    mflash_drv_init_internal() below) applied via NXP's standard macro
 *    rather than whole-object placement, decorated by explicit user
 *    instruction after flagging the risk (corrupting the running XIP
 *    image if code executes from flash while that same flash is mid
 *    erase/program). AT_QUICKACCESS_SECTION_CODE places into
 *    "CodeQuickAccess", which this project's linker script already
 *    routes to internal SRAM_DTC (confirmed: same section already used
 *    for the SDRAM-reliability fix -- see project memory
 *    project_sdram_unreliable.md).
 *
 * CONFIRMED WORKING ON REAL HARDWARE, 2026-07-13 (full littlefs
 * mount/format/write/close/remount/read-back round trip against this
 * exact file, via lfs_mflash.c). Ported into this scaffold repo
 * 2026-07-14 as the chosen flash backend for app_init.c's storage
 * stage, in preference to the scaffold's own earlier, never-tested
 * nand_log_flash_qspi.c (see that file's header for why it's not
 * currently wired in).
 */

#include <stdbool.h>

#include "mflash_drv.h"

#include "fsl_flexspi.h"
#include "fsl_cache.h"
#include "pin_mux.h"

#define NOR_CMD_LUT_SEQ_IDX_READ_NORMAL        7
#define NOR_CMD_LUT_SEQ_IDX_READ_FAST          13
#define NOR_CMD_LUT_SEQ_IDX_READ_FAST_QUAD     0
#define NOR_CMD_LUT_SEQ_IDX_READSTATUS         1
#define NOR_CMD_LUT_SEQ_IDX_WRITEENABLE        2
#define NOR_CMD_LUT_SEQ_IDX_ERASESECTOR        3
#define NOR_CMD_LUT_SEQ_IDX_PAGEPROGRAM_SINGLE 6
#define NOR_CMD_LUT_SEQ_IDX_PAGEPROGRAM_QUAD   4
#define NOR_CMD_LUT_SEQ_IDX_READID             8
#define NOR_CMD_LUT_SEQ_IDX_WRITESTATUSREG     9
#define NOR_CMD_LUT_SEQ_IDX_ENTERQPI           10
#define NOR_CMD_LUT_SEQ_IDX_EXITQPI            11
#define NOR_CMD_LUT_SEQ_IDX_READSTATUSREG      12
#define NOR_CMD_LUT_SEQ_IDX_ERASECHIP          5

#define CUSTOM_LUT_LENGTH        60
#define FLASH_BUSY_STATUS_POL    1
#define FLASH_BUSY_STATUS_OFFSET 0

#define FLASH_SIZE 0x4000 /* 128Mb / 1KByte -- matches this board's confirmed 16MB flash */

#ifndef XIP_EXTERNAL_FLASH
flexspi_device_config_t deviceconfig = {
    .flexspiRootClk       = 130909091, /* CONFIRMED: 1440000000/11 Hz, from board/clock_config.c */
    .flashSize            = FLASH_SIZE,
    .CSIntervalUnit       = kFLEXSPI_CsIntervalUnit1SckCycle,
    .CSInterval           = 2,
    .CSHoldTime           = 3,
    .CSSetupTime          = 3,
    .dataValidTime        = 0,
    .columnspace          = 0,
    .enableWordAddress    = 0,
    .AWRSeqIndex          = 0,
    .AWRSeqNumber         = 0,
    .ARDSeqIndex          = NOR_CMD_LUT_SEQ_IDX_READ_FAST_QUAD,
    .ARDSeqNumber         = 1,
    .AHBWriteWaitUnit     = kFLEXSPI_AhbWriteWaitUnit2AhbCycle,
    .AHBWriteWaitInterval = 0,
};
#endif

const uint32_t customLUT[CUSTOM_LUT_LENGTH] = {
    /* Normal read mode -SDR */
    [4 * NOR_CMD_LUT_SEQ_IDX_READ_NORMAL] =
        FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR, kFLEXSPI_1PAD, 0x03, kFLEXSPI_Command_RADDR_SDR, kFLEXSPI_1PAD, 0x18),
    [4 * NOR_CMD_LUT_SEQ_IDX_READ_NORMAL + 1] =
        FLEXSPI_LUT_SEQ(kFLEXSPI_Command_READ_SDR, kFLEXSPI_1PAD, 0x04, kFLEXSPI_Command_STOP, kFLEXSPI_1PAD, 0),

    /* Fast read mode - SDR */
    [4 * NOR_CMD_LUT_SEQ_IDX_READ_FAST] =
        FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR, kFLEXSPI_1PAD, 0x0B, kFLEXSPI_Command_RADDR_SDR, kFLEXSPI_1PAD, 0x18),
    [4 * NOR_CMD_LUT_SEQ_IDX_READ_FAST + 1] = FLEXSPI_LUT_SEQ(
        kFLEXSPI_Command_DUMMY_SDR, kFLEXSPI_1PAD, 0x08, kFLEXSPI_Command_READ_SDR, kFLEXSPI_1PAD, 0x04),

    /* Fast read quad mode - SDR */
    [4 * NOR_CMD_LUT_SEQ_IDX_READ_FAST_QUAD] =
        FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR, kFLEXSPI_1PAD, 0xEB, kFLEXSPI_Command_RADDR_SDR, kFLEXSPI_4PAD, 0x18),
    [4 * NOR_CMD_LUT_SEQ_IDX_READ_FAST_QUAD + 1] = FLEXSPI_LUT_SEQ(
        kFLEXSPI_Command_DUMMY_SDR, kFLEXSPI_4PAD, 0x06, kFLEXSPI_Command_READ_SDR, kFLEXSPI_4PAD, 0x04),

    /* Read extend parameters */
    [4 * NOR_CMD_LUT_SEQ_IDX_READSTATUS] =
        FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR, kFLEXSPI_1PAD, 0x81, kFLEXSPI_Command_READ_SDR, kFLEXSPI_1PAD, 0x04),

    /* Write Enable */
    [4 * NOR_CMD_LUT_SEQ_IDX_WRITEENABLE] =
        FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR, kFLEXSPI_1PAD, 0x06, kFLEXSPI_Command_STOP, kFLEXSPI_1PAD, 0),

    /* Erase Sector  */
    [4 * NOR_CMD_LUT_SEQ_IDX_ERASESECTOR] =
        FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR, kFLEXSPI_1PAD, 0x20, kFLEXSPI_Command_RADDR_SDR, kFLEXSPI_1PAD, 0x18),

    /* Page Program - single mode */
    [4 * NOR_CMD_LUT_SEQ_IDX_PAGEPROGRAM_SINGLE] =
        FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR, kFLEXSPI_1PAD, 0x02, kFLEXSPI_Command_RADDR_SDR, kFLEXSPI_1PAD, 0x18),
    [4 * NOR_CMD_LUT_SEQ_IDX_PAGEPROGRAM_SINGLE + 1] =
        FLEXSPI_LUT_SEQ(kFLEXSPI_Command_WRITE_SDR, kFLEXSPI_1PAD, 0x04, kFLEXSPI_Command_STOP, kFLEXSPI_1PAD, 0),

    /* Page Program - quad mode */
    [4 * NOR_CMD_LUT_SEQ_IDX_PAGEPROGRAM_QUAD] =
        FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR, kFLEXSPI_1PAD, 0x32, kFLEXSPI_Command_RADDR_SDR, kFLEXSPI_1PAD, 0x18),
    [4 * NOR_CMD_LUT_SEQ_IDX_PAGEPROGRAM_QUAD + 1] =
        FLEXSPI_LUT_SEQ(kFLEXSPI_Command_WRITE_SDR, kFLEXSPI_4PAD, 0x04, kFLEXSPI_Command_STOP, kFLEXSPI_1PAD, 0),

    /* Read ID */
    [4 * NOR_CMD_LUT_SEQ_IDX_READID] =
        FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR, kFLEXSPI_1PAD, 0x9F, kFLEXSPI_Command_READ_SDR, kFLEXSPI_1PAD, 0x04),

    /* Enable Quad mode */
    [4 * NOR_CMD_LUT_SEQ_IDX_WRITESTATUSREG] =
        FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR, kFLEXSPI_1PAD, 0x01, kFLEXSPI_Command_WRITE_SDR, kFLEXSPI_1PAD, 0x04),

    /* Enter QPI mode */
    [4 * NOR_CMD_LUT_SEQ_IDX_ENTERQPI] =
        FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR, kFLEXSPI_1PAD, 0x35, kFLEXSPI_Command_STOP, kFLEXSPI_1PAD, 0),

    /* Exit QPI mode */
    [4 * NOR_CMD_LUT_SEQ_IDX_EXITQPI] =
        FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR, kFLEXSPI_4PAD, 0xF5, kFLEXSPI_Command_STOP, kFLEXSPI_1PAD, 0),

    /* Read status register */
    [4 * NOR_CMD_LUT_SEQ_IDX_READSTATUSREG] =
        FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR, kFLEXSPI_1PAD, 0x05, kFLEXSPI_Command_READ_SDR, kFLEXSPI_1PAD, 0x04),

    /* Erase whole chip */
    [4 * NOR_CMD_LUT_SEQ_IDX_ERASECHIP] =
        FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR, kFLEXSPI_1PAD, 0xC7, kFLEXSPI_Command_STOP, kFLEXSPI_1PAD, 0),
};

AT_QUICKACCESS_SECTION_CODE(static status_t flexspi_nor_wait_bus_busy(FLEXSPI_Type *base))
{
    /* Wait status ready. */
    bool isBusy;
    uint32_t readValue;
    status_t status;
    flexspi_transfer_t flashXfer;

    flashXfer.deviceAddress = 0;
    flashXfer.port          = kFLEXSPI_PortA1;
    flashXfer.cmdType       = kFLEXSPI_Read;
    flashXfer.SeqNumber     = 1;
    flashXfer.seqIndex      = NOR_CMD_LUT_SEQ_IDX_READSTATUSREG;
    flashXfer.data          = &readValue;
    flashXfer.dataSize      = 1;

    do
    {
        status = FLEXSPI_TransferBlocking(base, &flashXfer);

        if (status != kStatus_Success)
        {
            return status;
        }
        if (FLASH_BUSY_STATUS_POL)
        {
            isBusy = (readValue & (1U << FLASH_BUSY_STATUS_OFFSET)) ? true : false;
        }
        else
        {
            isBusy = (readValue & (1U << FLASH_BUSY_STATUS_OFFSET)) ? false : true;
        }

    } while (isBusy);

    return status;
}

AT_QUICKACCESS_SECTION_CODE(static status_t flexspi_nor_write_enable(FLEXSPI_Type *base, uint32_t address))
{
    flexspi_transfer_t flashXfer;
    status_t status;

    flashXfer.deviceAddress = address;
    flashXfer.port          = kFLEXSPI_PortA1;
    flashXfer.cmdType       = kFLEXSPI_Command;
    flashXfer.SeqNumber     = 1;
    flashXfer.seqIndex      = NOR_CMD_LUT_SEQ_IDX_WRITEENABLE;

    status = FLEXSPI_TransferBlocking(base, &flashXfer);

    return status;
}

AT_QUICKACCESS_SECTION_CODE(status_t flexspi_nor_enable_quad_mode(FLEXSPI_Type *base))
{
    flexspi_transfer_t flashXfer;
    status_t status;
    uint32_t writeValue = 0x40;

    status = flexspi_nor_write_enable(base, 0);
    if (status != kStatus_Success)
    {
        return status;
    }

    flashXfer.deviceAddress = 0;
    flashXfer.port          = kFLEXSPI_PortA1;
    flashXfer.cmdType       = kFLEXSPI_Write;
    flashXfer.SeqNumber     = 1;
    flashXfer.seqIndex      = NOR_CMD_LUT_SEQ_IDX_WRITESTATUSREG;
    flashXfer.data          = &writeValue;
    flashXfer.dataSize      = 1;

    status = FLEXSPI_TransferBlocking(base, &flashXfer);
    if (status != kStatus_Success)
    {
        return status;
    }

    status = flexspi_nor_wait_bus_busy(base);

    return status;
}

AT_QUICKACCESS_SECTION_CODE(static status_t flexspi_nor_flash_sector_erase(FLEXSPI_Type *base, uint32_t address))
{
    status_t status;
    flexspi_transfer_t flashXfer;

    status = flexspi_nor_write_enable(base, address);
    if (status != kStatus_Success)
    {
        return status;
    }

    flashXfer.deviceAddress = address;
    flashXfer.port          = kFLEXSPI_PortA1;
    flashXfer.cmdType       = kFLEXSPI_Command;
    flashXfer.SeqNumber     = 1;
    flashXfer.seqIndex      = NOR_CMD_LUT_SEQ_IDX_ERASESECTOR;
    status                  = FLEXSPI_TransferBlocking(base, &flashXfer);
    if (status != kStatus_Success)
    {
        return status;
    }

    status = flexspi_nor_wait_bus_busy(base);

    return status;
}

AT_QUICKACCESS_SECTION_CODE(static status_t flexspi_nor_flash_page_program(FLEXSPI_Type *base, uint32_t address, const uint32_t *src))
{
    status_t status;
    flexspi_transfer_t flashXfer;

    status = flexspi_nor_write_enable(base, address);
    if (status != kStatus_Success)
    {
        return status;
    }

    flashXfer.deviceAddress = address;
    flashXfer.port          = kFLEXSPI_PortA1;
    flashXfer.cmdType       = kFLEXSPI_Write;
    flashXfer.SeqNumber     = 1;
    flashXfer.seqIndex      = NOR_CMD_LUT_SEQ_IDX_PAGEPROGRAM_QUAD;
    flashXfer.data          = (uint32_t *)src;
    flashXfer.dataSize      = MFLASH_PAGE_SIZE;
    status                  = FLEXSPI_TransferBlocking(base, &flashXfer);
    if (status != kStatus_Success)
    {
        return status;
    }

    status = flexspi_nor_wait_bus_busy(base);

    return status;
}

AT_QUICKACCESS_SECTION_CODE(static status_t flexspi_nor_read_data(FLEXSPI_Type *base, uint32_t startAddress, uint32_t *buffer, uint32_t length))
{
    status_t status;
    flexspi_transfer_t flashXfer;

    flashXfer.deviceAddress = startAddress;
    flashXfer.port          = kFLEXSPI_PortA1;
    flashXfer.cmdType       = kFLEXSPI_Read;
    flashXfer.SeqNumber     = 1;
    flashXfer.seqIndex      = NOR_CMD_LUT_SEQ_IDX_READ_FAST_QUAD;
    flashXfer.data          = buffer;
    flashXfer.dataSize      = length;

    status = FLEXSPI_TransferBlocking(base, &flashXfer);

    return status;
}

/* Initialize flash peripheral,
 * cannot be invoked directly, requires calling wrapper in non XIP memory */
AT_QUICKACCESS_SECTION_CODE(static int32_t mflash_drv_init_internal(void))
{
    /* NOTE: Multithread access is not supported for SRAM target.
     *       XIP target MUST be protected by disabling global interrupts
     *       since all ISR (and API that is used inside) is placed at XIP.
     *       It is necessary to place at least "mflash_drv.o", "fsl_flexspi.o" to SRAM */
    uint32_t primask = __get_PRIMASK();

    __asm("cpsid i");

    status_t status = kStatus_Success;

#ifndef XIP_EXTERNAL_FLASH
    flexspi_config_t config;

    FLEXSPI_GetDefaultConfig(&config);

    config.ahbConfig.enableAHBPrefetch   = true;
    config.ahbConfig.enableAHBBufferable = true;
    config.ahbConfig.enableAHBCachable   = true;
    config.rxSampleClock                 = kFLEXSPI_ReadSampleClkLoopbackFromDqsPad;
    FLEXSPI_Init(MFLASH_FLEXSPI, &config);

    MFLASH_FLEXSPI->AHBCR |= FLEXSPI_AHBCR_READADDROPT_MASK;

    FLEXSPI_SetFlashConfig(MFLASH_FLEXSPI, &deviceconfig, kFLEXSPI_PortA1);
#endif

    FLEXSPI_UpdateLUT(MFLASH_FLEXSPI, 0, customLUT, CUSTOM_LUT_LENGTH);

#ifndef XIP_EXTERNAL_FLASH
    status = flexspi_nor_enable_quad_mode(MFLASH_FLEXSPI);
#endif

    if (primask == 0)
    {
        __asm("cpsie i");
    }

    return status;
}

int32_t mflash_drv_init(void)
{
    return mflash_drv_init_internal();
}

AT_QUICKACCESS_SECTION_CODE(static int32_t mflash_drv_sector_erase_internal(uint32_t sector_addr))
{
    status_t status;
    uint32_t primask = __get_PRIMASK();

    __asm("cpsid i");

    status = flexspi_nor_flash_sector_erase(MFLASH_FLEXSPI, sector_addr);

    /* FIXED (2026-07-13): was FLEXSPI_SoftwareReset(MFLASH_FLEXSPI) -- that
     * function is `static inline` in fsl_flexspi.h, but this build did NOT
     * actually inline it (confirmed via a real HardFault: disassembly
     * showed a `bl` through a linker-generated veneer to an out-of-line
     * copy sitting in flash, not RAM). Calling flash-resident code from
     * here defeats the whole point of RAM-placing this function -- so its
     * trivial body (2 lines) is inlined by hand instead, guaranteeing it
     * stays in RAM regardless of what the compiler decides to do with the
     * header's "inline" hint. */
    MFLASH_FLEXSPI->MCR0 |= FLEXSPI_MCR0_SWRESET_MASK;
    while (0U != (MFLASH_FLEXSPI->MCR0 & FLEXSPI_MCR0_SWRESET_MASK))
    {
    }

    DCACHE_InvalidateByRange(MFLASH_BASE_ADDRESS + sector_addr, MFLASH_SECTOR_SIZE);

    if (primask == 0)
    {
        __asm("cpsie i");
    }

    __ISB();

    return status;
}

int32_t mflash_drv_sector_erase(uint32_t sector_addr)
{
    if (0 == mflash_drv_is_sector_aligned(sector_addr))
        return kStatus_InvalidArgument;

    return mflash_drv_sector_erase_internal(sector_addr);
}

AT_QUICKACCESS_SECTION_CODE(static int32_t mflash_drv_page_program_internal(uint32_t page_addr, uint32_t *data))
{
    uint32_t primask = __get_PRIMASK();

    __asm("cpsid i");

    status_t status;
    status = flexspi_nor_flash_page_program(MFLASH_FLEXSPI, page_addr, data);

    /* FIXED (2026-07-13): was FLEXSPI_SoftwareReset(MFLASH_FLEXSPI) -- that
     * function is `static inline` in fsl_flexspi.h, but this build did NOT
     * actually inline it (confirmed via a real HardFault: disassembly
     * showed a `bl` through a linker-generated veneer to an out-of-line
     * copy sitting in flash, not RAM). Calling flash-resident code from
     * here defeats the whole point of RAM-placing this function -- so its
     * trivial body (2 lines) is inlined by hand instead, guaranteeing it
     * stays in RAM regardless of what the compiler decides to do with the
     * header's "inline" hint. */
    MFLASH_FLEXSPI->MCR0 |= FLEXSPI_MCR0_SWRESET_MASK;
    while (0U != (MFLASH_FLEXSPI->MCR0 & FLEXSPI_MCR0_SWRESET_MASK))
    {
    }

    DCACHE_InvalidateByRange(MFLASH_BASE_ADDRESS + page_addr, MFLASH_PAGE_SIZE);

    if (primask == 0)
    {
        __asm("cpsie i");
    }

    __ISB();

    return status;
}

/* NOTE: Don't try to store constant data that are located in XIP !! */
int32_t mflash_drv_page_program(uint32_t page_addr, uint32_t *data)
{
    if (0 == mflash_drv_is_page_aligned(page_addr))
        return kStatus_InvalidArgument;

    return mflash_drv_page_program_internal(page_addr, data);
}

AT_QUICKACCESS_SECTION_CODE(static int32_t mflash_drv_read_internal(uint32_t addr, uint32_t *buffer, uint32_t len))
{
    uint32_t primask = __get_PRIMASK();

    __asm("cpsid i");

    status_t status;
    status = flexspi_nor_read_data(MFLASH_FLEXSPI, addr, buffer, len);

    /* FIXED (2026-07-13): was FLEXSPI_SoftwareReset(MFLASH_FLEXSPI) -- that
     * function is `static inline` in fsl_flexspi.h, but this build did NOT
     * actually inline it (confirmed via a real HardFault: disassembly
     * showed a `bl` through a linker-generated veneer to an out-of-line
     * copy sitting in flash, not RAM). Calling flash-resident code from
     * here defeats the whole point of RAM-placing this function -- so its
     * trivial body (2 lines) is inlined by hand instead, guaranteeing it
     * stays in RAM regardless of what the compiler decides to do with the
     * header's "inline" hint. */
    MFLASH_FLEXSPI->MCR0 |= FLEXSPI_MCR0_SWRESET_MASK;
    while (0U != (MFLASH_FLEXSPI->MCR0 & FLEXSPI_MCR0_SWRESET_MASK))
    {
    }

    if (primask == 0)
    {
        __asm("cpsie i");
    }

    __ISB();

    return status;
}

int32_t mflash_drv_read(uint32_t addr, uint32_t *buffer, uint32_t len)
{
    if (((uint32_t)buffer % 4) || (len % 4))
        return kStatus_InvalidArgument;

    return mflash_drv_read_internal(addr, buffer, len);
}

void *mflash_drv_phys2log(uint32_t addr, uint32_t len)
{
    uint32_t remap_offset = IOMUXC_GPR->GPR32 & 0xFFFFF000;
    uint32_t remap_start  = IOMUXC_GPR->GPR30 & 0xFFFFF000;
    uint32_t remap_end    = IOMUXC_GPR->GPR31 & 0xFFFFF000;

    uint32_t bus_addr = addr + MFLASH_BASE_ADDRESS;

    if (remap_offset == 0 || (remap_end <= remap_start))
    {
        return (void *)bus_addr;
    }

    if ((remap_start >= bus_addr + len) || (remap_end <= bus_addr))
    {
        return (void *)bus_addr;
    }

    if ((remap_start + remap_offset <= bus_addr) && (remap_end + remap_offset >= bus_addr + len))
    {
        return (void *)(bus_addr - remap_offset);
    }

    return NULL;
}

uint32_t mflash_drv_log2phys(void *ptr, uint32_t len)
{
    uint32_t remap_offset = IOMUXC_GPR->GPR32 & 0xFFFFF000;
    uint32_t remap_start  = IOMUXC_GPR->GPR30 & 0xFFFFF000;
    uint32_t remap_end    = IOMUXC_GPR->GPR31 & 0xFFFFF000;

    uint32_t bus_addr = (uint32_t)ptr;

    if (bus_addr < MFLASH_BASE_ADDRESS)
    {
        return UINT32_MAX;
    }

    if (remap_offset == 0 || (remap_end <= remap_start))
    {
        return (bus_addr - MFLASH_BASE_ADDRESS);
    }

    if ((remap_start >= bus_addr + len) || (remap_end <= bus_addr))
    {
        return (bus_addr - MFLASH_BASE_ADDRESS);
    }

    if ((remap_start <= bus_addr) && (remap_end >= bus_addr + len))
    {
        return (bus_addr + remap_offset - MFLASH_BASE_ADDRESS);
    }

    return UINT32_MAX;
}
