#pragma once

// =============================================================================
//  MapPersist — EEPROM persistence for the evac-zone map + pose snapshot.
//
//  Layout (starting at EEPROM_MAP_BASE = 0x0020):
//      MapHeader   header   (40 B: magic, version, pose, P-diag, flags, run_count)
//      uint8_t     semantic[MAP_N]   (1600 B for 40x40)
//      uint32_t    crc32   (4 B over header + semantic)
//
//  Wear minimisation: an in-RAM shadow of the last-saved semantic array drives
//  byte-level diffing — only changed bytes get EEPROM.update()'d.
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
    uint32_t flags;
    uint16_t run_count;
    uint16_t reserved1;
};

bool     mapPersistInit();
bool     mapPersistLoad(Pose& p, EvacMap& m);
uint16_t mapPersistSave(const Pose& p, const EvacMap& m);
uint16_t mapPersistDirtyCount(const EvacMap& m);
