/*
 * firmware_version.h
 *
 * Was `_FIRMWARE_VERSION_` -- used but never defined anywhere in the
 * pasted original source (ACTIVERFID_V1.02_UHF.c references it at
 * lines 1643/3341/3476 for the splash string, the update-available
 * check, and the "current firmware" display, but its #define was never
 * shown). Per explicit instruction: 0x0103 (V1.03) for now, bump this
 * one place on release. High byte = major, low byte = minor, matching
 * the original's own "%02d.%02d", version >> 8, version & 0xFF format.
 */

#ifndef FIRMWARE_VERSION_H
#define FIRMWARE_VERSION_H

#define APP_FIRMWARE_VERSION 0x0103u

#endif /* FIRMWARE_VERSION_H */
