#ifndef MapPersist_h
#define MapPersist_h

// =============================================================================
//  MapPersist — EEPROM persistence for the evacuation-zone map + pose snapshot.
//
//  Layout (starting at EEPROM_MAP_BASE = 0x0020):
//      MapHeader   header   (40 B: magic, version, pose, P-diag, flags, run_count)
//      uint8_t     semantic[MAP_N]   (1600 B for 40x40)
//      uint32_t    crc32   (4 B over header + semantic)
//
//  Wear minimisation:
//    - Internal "shadow" mirror of the semantic array; saveDirtyOnly() walks
//      it and only writes changed bytes via EEPROM.update().
//    - mapPersistDirtyCount() lets the caller decide when to checkpoint.
//
//  Phase 1 wires this into Mapping.ino for save-on-exit + watchdog checkpoint.
// =============================================================================

#include <Arduino.h>
#include "config.h"
#include "Pose.h"
#include "MapGrid.h"

struct MapHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved0;
    float    pose_x;
    float    pose_y;
    float    pose_theta;
    float    pose_var_x;
    float    pose_var_y;
    float    pose_var_th;
    uint32_t flags;        // bit 0 = persisted; reserved otherwise
    uint16_t run_count;
    uint16_t reserved1;
};

// One-time module init. Reads existing semantic from EEPROM into the shadow.
// Returns true if a valid map was loaded into the shadow (caller may then
// copy via mapPersistLoad), false if EEPROM is uninitialised.
bool mapPersistInit();

// Load EEPROM contents into pose + map. Returns false on magic/CRC mismatch.
// Log-odds are zeroed; caller should let ToF refresh the belief.
bool mapPersistLoad(Pose& p, EvacMap& m);

// Save current pose + map to EEPROM. Writes only changed semantic bytes
// against the shadow, then rewrites header + CRC.
// Returns the number of byte-writes performed (for wear logging).
uint16_t mapPersistSave(const Pose& p, const EvacMap& m);

// Number of semantic bytes currently differing from the last saved shadow.
uint16_t mapPersistDirtyCount(const EvacMap& m);

#endif
