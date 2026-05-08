#include "MapPersist.h"
#include <EEPROM.h>

// =============================================================================
//  MapPersist implementation
// =============================================================================

namespace {
    // In-RAM shadow of the most recently saved semantic array.
    // Used to compute byte-level diffs and minimise EEPROM writes.
    uint8_t  _shadow[MAP_N];
    bool     _shadow_loaded = false;

    constexpr uint16_t HDR_ADDR = (uint16_t)EEPROM_MAP_BASE;
    constexpr uint16_t SEM_ADDR = HDR_ADDR + (uint16_t)sizeof(MapHeader);
    constexpr uint16_t CRC_ADDR = SEM_ADDR + MAP_N;

    // ---- CRC32 (IEEE 802.3 / Ethernet, polynomial 0xEDB88320, no table) ----
    uint32_t crc32_update(uint32_t crc, uint8_t b) {
        crc ^= b;
        for (uint8_t i = 0; i < 8; ++i) {
            crc = (crc >> 1) ^ (0xEDB88320UL & -(int32_t)(crc & 1));
        }
        return crc;
    }

    uint32_t crc32_compute_eeprom(uint16_t addr, uint16_t length) {
        uint32_t crc = 0xFFFFFFFFUL;
        for (uint16_t i = 0; i < length; ++i) {
            crc = crc32_update(crc, EEPROM.read(addr + i));
        }
        return crc ^ 0xFFFFFFFFUL;
    }

    uint32_t crc32_compute_buf(const uint8_t* buf, uint16_t length, uint32_t crc_in) {
        uint32_t crc = crc_in;
        for (uint16_t i = 0; i < length; ++i) crc = crc32_update(crc, buf[i]);
        return crc;
    }
}

// ---------------------------------------------------------------------------

bool mapPersistInit() {
    MapHeader hdr;
    EEPROM.get(HDR_ADDR, hdr);

    if (hdr.magic != EEPROM_MAP_MAGIC || hdr.version != EEPROM_MAP_VERSION) {
        // Initialise shadow to all-UNKNOWN so first save records every cell.
        for (uint16_t i = 0; i < MAP_N; ++i) _shadow[i] = CS_UNKNOWN;
        _shadow_loaded = false;
        return false;
    }

    // Validate CRC.
    uint32_t want = 0;
    EEPROM.get(CRC_ADDR, want);
    const uint32_t got = crc32_compute_eeprom(HDR_ADDR, sizeof(MapHeader) + MAP_N);
    if (want != got) {
        for (uint16_t i = 0; i < MAP_N; ++i) _shadow[i] = CS_UNKNOWN;
        _shadow_loaded = false;
        return false;
    }

    // Pull semantic into shadow.
    for (uint16_t i = 0; i < MAP_N; ++i) _shadow[i] = EEPROM.read(SEM_ADDR + i);
    _shadow_loaded = true;
    return true;
}

bool mapPersistLoad(Pose& p, EvacMap& m) {
    if (!mapPersistInit()) return false;

    MapHeader hdr;
    EEPROM.get(HDR_ADDR, hdr);

    p.x_mm  = hdr.pose_x;
    p.y_mm  = hdr.pose_y;
    p.theta = hdr.pose_theta;
    for (int i = 0; i < 9; ++i) p.P[i] = 0.0f;
    p.P[0] = hdr.pose_var_x;
    p.P[4] = hdr.pose_var_y;
    p.P[8] = hdr.pose_var_th;

    for (uint16_t i = 0; i < MAP_N; ++i) {
        m.semantic[i] = _shadow[i];
        m.logodds [i] = 0;
    }
    return true;
}

uint16_t mapPersistDirtyCount(const EvacMap& m) {
    uint16_t n = 0;
    for (uint16_t i = 0; i < MAP_N; ++i) {
        if (m.semantic[i] != _shadow[i]) ++n;
    }
    return n;
}

uint16_t mapPersistSave(const Pose& p, const EvacMap& m) {
    uint16_t writes = 0;

    // 1. Diff semantic against shadow; write only changed bytes.
    for (uint16_t i = 0; i < MAP_N; ++i) {
        if (m.semantic[i] != _shadow[i]) {
            EEPROM.update(SEM_ADDR + i, m.semantic[i]);
            _shadow[i] = m.semantic[i];
            ++writes;
        }
    }

    // 2. Header. EEPROM.put uses update() internally so unchanged bytes don't
    //    incur a flash write.
    MapHeader hdr;
    hdr.magic       = EEPROM_MAP_MAGIC;
    hdr.version     = EEPROM_MAP_VERSION;
    hdr.reserved0   = 0;
    hdr.pose_x      = p.x_mm;
    hdr.pose_y      = p.y_mm;
    hdr.pose_theta  = p.theta;
    hdr.pose_var_x  = p.P[0];
    hdr.pose_var_y  = p.P[4];
    hdr.pose_var_th = p.P[8];
    hdr.flags       = 0x1UL;

    // Pull existing run_count, increment.
    MapHeader prev;
    EEPROM.get(HDR_ADDR, prev);
    hdr.run_count = (prev.magic == EEPROM_MAP_MAGIC) ? (uint16_t)(prev.run_count + 1) : 1;
    hdr.reserved1 = 0;
    EEPROM.put(HDR_ADDR, hdr);

    // 3. CRC over header + semantic (read back from EEPROM so we see the
    //    actual stored bytes including any update() no-ops).
    uint32_t crc = 0xFFFFFFFFUL;
    crc = crc32_compute_buf((const uint8_t*)&hdr, sizeof(hdr), crc);
    crc = crc32_compute_buf(_shadow, MAP_N, crc);
    crc ^= 0xFFFFFFFFUL;
    EEPROM.put(CRC_ADDR, crc);

    _shadow_loaded = true;
    return writes;
}
