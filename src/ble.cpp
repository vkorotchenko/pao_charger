#include "ble.h"
#include "config.h"
#include "version.h"

extern bool chargerEnabled;

// ---------------------------------------------------------------------------
// Compile-time clamp: each FW_VERSION_* field is packed as a single uint8.
// If we ever cross 255 in a field, take the cap and keep going — Phase 1
// never asks for more precision than that.
// ---------------------------------------------------------------------------
static inline uint8_t clampU8(int v) {
    if (v < 0)   return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

// ---------------------------------------------------------------------------
// Helpers: pack values as raw big-endian binary and set/notify
// ---------------------------------------------------------------------------

void Ble::setU16(NimBLECharacteristic* c, uint16_t v, bool notify) {
    uint8_t buf[2] = { (uint8_t)(v >> 8), (uint8_t)(v & 0xFF) };
    c->setValue(buf, 2);
    if (notify) c->notify();
}

void Ble::setU8(NimBLECharacteristic* c, uint8_t v, bool notify) {
    c->setValue(&v, 1);
    if (notify) c->notify();
}

void Ble::setU32(NimBLECharacteristic* c, uint32_t v, bool notify) {
    uint8_t buf[4] = {
        (uint8_t)(v >> 24), (uint8_t)(v >> 16),
        (uint8_t)(v >>  8), (uint8_t)(v      )
    };
    c->setValue(buf, 4);
    if (notify) c->notify();
}

// 0xFF25 firmware version: 4 bytes little-endian — [major, minor, patch, build].
// Each field is clamped to 255 (uint8 wire width). build = commits-since-tag,
// 0 for a clean tag.
void Ble::setFwVersion(NimBLECharacteristic* c, bool notify) {
    uint8_t buf[4] = {
        clampU8(FW_VERSION_MAJOR),
        clampU8(FW_VERSION_MINOR),
        clampU8(FW_VERSION_PATCH),
        clampU8(FW_VERSION_BUILD),
    };
    c->setValue(buf, 4);
    if (notify) c->notify();
}

// ---------------------------------------------------------------------------
// Write callbacks — one class per writable characteristic
// ---------------------------------------------------------------------------

class AmpWriteCallback : public NimBLECharacteristicCallbacks {
    NimBLECharacteristic* _echo;
public:
    AmpWriteCallback(NimBLECharacteristic* echo) : _echo(echo) {}
    void onWrite(NimBLECharacteristic* pChar) override {
        auto val = pChar->getValue();
        if (val.size() < 2) return;
        uint16_t v = ((uint16_t)val[0] << 8) | val[1];
        int prev = Config::getMaxCurrent();
        int prevTgtV = Config::getTargetVoltage();
        Config::setMaxCurrent((int)v);
        Logger::log(LOG_CAT_BLE, "BLE SET max_current: %d -> %d (1/10th A). targetV: %d -> %d",
                    prev, (int)v, prevTgtV, Config::getTargetVoltage());
        uint8_t buf[2] = { val[0], val[1] };
        _echo->setValue(buf, 2);
        _echo->notify();
    }
};

class PctWriteCallback : public NimBLECharacteristicCallbacks {
    NimBLECharacteristic* _echo;
public:
    PctWriteCallback(NimBLECharacteristic* echo) : _echo(echo) {}
    void onWrite(NimBLECharacteristic* pChar) override {
        auto val = pChar->getValue();
        if (val.size() < 2) return;
        uint16_t v = ((uint16_t)val[0] << 8) | val[1];
        int prevPctX1000 = (int)(Config::getTargetPercentage() * 1000);
        int prevTgtV = Config::getTargetVoltage();
        Config::setTargetPercentage((float)v / 1000.0f);
        Logger::log(LOG_CAT_BLE, "BLE SET target_pct: %d -> %d (pct*1000). targetV: %d -> %d",
                    prevPctX1000, (int)v, prevTgtV, Config::getTargetVoltage());
        uint8_t buf[2] = { val[0], val[1] };
        _echo->setValue(buf, 2);
        _echo->notify();
    }
};

class TimeWriteCallback : public NimBLECharacteristicCallbacks {
    NimBLECharacteristic* _echo;
public:
    TimeWriteCallback(NimBLECharacteristic* echo) : _echo(echo) {}
    void onWrite(NimBLECharacteristic* pChar) override {
        auto val = pChar->getValue();
        if (val.size() < 2) return;
        uint16_t v = ((uint16_t)val[0] << 8) | val[1];
        int prev = Config::getMaxChargeTime();
        Config::setMaxChargeTime((int)v);
        Logger::log(LOG_CAT_BLE, "BLE SET max_time: %d -> %d s (0=no limit)", prev, (int)v);
        uint8_t buf[2] = { val[0], val[1] };
        _echo->setValue(buf, 2);
        _echo->notify();
    }
};

// 0xFF05 generic config-cmd dispatcher.
// Wire format from mobile (writeConfigCmd): [cmdId, 0, valueHi, valueLo].
// cmdId 5 = reset all stored config to compile-time defaults, then reboot so
// every subsystem re-reads its values cleanly on the next boot.
class ConfigCmdWriteCallback : public NimBLECharacteristicCallbacks {
public:
    void onWrite(NimBLECharacteristic* pChar) override {
        auto val = pChar->getValue();
        if (val.size() < 1) return;
        uint8_t cmd = val[0];
        Logger::log(LOG_CAT_BLE, "BLE CMD cmd=%d (len=%d)", (int)cmd, (int)val.size());
        switch (cmd) {
            case 5:
                Logger::log(LOG_CAT_BLE, "BLE CMD: reset to defaults — clearing prefs and rebooting");
                Config::resetToDefaults();
                delay(100);  // let the log flush before reset
                ESP.restart();
                break;
            default:
                Logger::log(LOG_CAT_BLE, "BLE CMD: unknown cmd %d — ignored", (int)cmd);
                break;
        }
    }
};

class OnOffWriteCallback : public NimBLECharacteristicCallbacks {
public:
    void onWrite(NimBLECharacteristic* pChar) override {
        auto val = pChar->getValue();
        if (val.size() < 1) return;
        bool prev = chargerEnabled;
        chargerEnabled = (val[0] != 0);
        Logger::log(LOG_CAT_BLE, "BLE SET charger on/off: %d -> %d", (int)prev, (int)chargerEnabled);
    }
};

// ---------------------------------------------------------------------------
// Server callbacks — seed characteristics on connect, restart adv on disconnect
// ---------------------------------------------------------------------------

class BleServerCallbacks : public NimBLEServerCallbacks {
    Ble* _ble;
public:
    BleServerCallbacks(Ble* ble) : _ble(ble) {}

    void onConnect(NimBLEServer* pServer) override {
        Logger::log(LOG_CAT_BLE, "BLE client connected");
        _ble->seedReadableChars();
    }

    void onDisconnect(NimBLEServer* pServer) override {
        Logger::log(LOG_CAT_BLE, "BLE client disconnected, restarting advertising");
        NimBLEDevice::startAdvertising();
    }
};

// ---------------------------------------------------------------------------
// Ble::seedReadableChars — populate all READ characteristics with current config
// ---------------------------------------------------------------------------

void Ble::seedReadableChars() {
    setU16(pTVolt,   (uint16_t)Config::getTargetVoltage());
    setU16(pTAmp,    (uint16_t)Config::getMaxCurrent());
    setU16(pNomV,    (uint16_t)Config::getNominalVoltage());
    setU8 (pMaxMult, (uint8_t)(Config::getNominalMaxMultiplier() * 100));
    setU8 (pMinMult, (uint8_t)(Config::getNominalMinMultiplier() * 100));
    setU16(pAbsMaxV, (uint16_t)Config::getMaxVoltage());
    setU16(pAbsMinV, (uint16_t)Config::getMinVoltage());
    setU16(pCfgAmp,  (uint16_t)Config::getMaxCurrent());
    setU16(pCfgPct,  (uint16_t)(Config::getTargetPercentage() * 1000));
    setU16(pCfgTime, (uint16_t)Config::getMaxChargeTime());
    uint8_t onOffVal = 0x01;
    pOnOff->setValue(&onOffVal, 1);

    // Firmware version — seed value AND notify subscribers (if any) on every
    // connect, so the mobile app sees the current charger version as soon as
    // the GATT service is up.
    setFwVersion(pFwVer, /*notify=*/true);
}

// ---------------------------------------------------------------------------
// Ble::setup — initialise NimBLE GATT server and start advertising
// ---------------------------------------------------------------------------

void Ble::setup() {
    NimBLEDevice::init(DISPLAY_NAME);

    NimBLEServer* pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new BleServerCallbacks(this));

    NimBLEService* pSvc = pServer->createService("27B0");

    // Live telemetry
    pTVolt = pSvc->createCharacteristic("2A1B", NIMBLE_PROPERTY::READ);
    pTAmp  = pSvc->createCharacteristic("2A1A", NIMBLE_PROPERTY::READ);
    pCVolt = pSvc->createCharacteristic("2BED", NIMBLE_PROPERTY::NOTIFY);
    pCAmp  = pSvc->createCharacteristic("2BF0", NIMBLE_PROPERTY::NOTIFY);
    pRTime = pSvc->createCharacteristic("2BEE", NIMBLE_PROPERTY::NOTIFY);

    // Config (writable)
    pCfgAmp  = pSvc->createCharacteristic("FF01",
                   NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY);
    pCfgPct  = pSvc->createCharacteristic("FF02",
                   NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY);
    pCfgTime = pSvc->createCharacteristic("FF03",
                   NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY);
    pCfgCmd  = pSvc->createCharacteristic("FF05",
                   NIMBLE_PROPERTY::WRITE);
    pOnOff   = pSvc->createCharacteristic("FF06",
                   NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY);

    pCfgAmp ->setCallbacks(new AmpWriteCallback(pCfgAmp));
    pCfgPct ->setCallbacks(new PctWriteCallback(pCfgPct));
    pCfgTime->setCallbacks(new TimeWriteCallback(pCfgTime));
    pCfgCmd ->setCallbacks(new ConfigCmdWriteCallback());
    pOnOff  ->setCallbacks(new OnOffWriteCallback());

    // Status
    pChgState = pSvc->createCharacteristic("FF10", NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    pSOC      = pSvc->createCharacteristic("FF11", NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    pError    = pSvc->createCharacteristic("FF12", NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);

    // Battery info (read only)
    pNomV    = pSvc->createCharacteristic("FF20", NIMBLE_PROPERTY::READ);
    pMaxMult = pSvc->createCharacteristic("FF21", NIMBLE_PROPERTY::READ);
    pMinMult = pSvc->createCharacteristic("FF22", NIMBLE_PROPERTY::READ);
    pAbsMaxV = pSvc->createCharacteristic("FF23", NIMBLE_PROPERTY::READ);
    pAbsMinV = pSvc->createCharacteristic("FF24", NIMBLE_PROPERTY::READ);

    // Firmware version (read + notify) — 4 bytes little-endian: maj,min,patch,build.
    pFwVer   = pSvc->createCharacteristic("FF25",
                   NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);

    seedReadableChars();

    Logger::log(LOG_CAT_BLE, "BLE firmware version: %s (sha=%s)",
                FW_VERSION_STRING, FW_VERSION_GIT_SHA);

    pSvc->start();

    NimBLEAdvertising* pAdv = NimBLEDevice::getAdvertising();
    pAdv->addServiceUUID("27B0");
    pAdv->setScanResponse(true);
    NimBLEDevice::startAdvertising();

    Logger::log(LOG_CAT_BLE, "BLE advertising started as '%s'", DISPLAY_NAME);
}

// ---------------------------------------------------------------------------
// Ble::poll — no-op: NimBLE runs on its own FreeRTOS task
// ---------------------------------------------------------------------------

void Ble::poll() {}

// ---------------------------------------------------------------------------
// Ble::loop — push characteristic updates (~1 s fast, ~5 s slow)
// ---------------------------------------------------------------------------

void Ble::loop(int tVolt, int tAmp, int cVolt, int cAmp,
               unsigned long running_time, bool isCharging, int soc, int error_state) {

    bool connected = NimBLEDevice::getServer()->getConnectedCount() > 0;

    Logger::log(LOG_CAT_BLE, "BLE loop: conn=%d cV=%d cA=%d tV=%d isChg=%d on=%d soc=%d err=%d",
                (int)connected, cVolt, cAmp, tVolt, (int)isCharging,
                (int)chargerEnabled, soc, error_state);

    if (!connected) return;

    // Fast group (~1 s)
    setU16(pCVolt, (uint16_t)cVolt, true);
    setU16(pCAmp,  (uint16_t)cAmp,  true);
    setU32(pRTime, (uint32_t)running_time, true);

    uint8_t chargeStateVal = (isCharging && chargerEnabled) ? 0 : 1;
    setU8(pChgState, chargeStateVal, true);

    uint8_t socPct = (uint8_t)min(100, soc * 25);
    setU8(pSOC, socPct, true);

    setU8(pError, (uint8_t)(error_state & 0xFF), true);

    Logger::log(LOG_CAT_BLE, "BLE chargeState=%d socPct=%d err=%d",
                (int)chargeStateVal, (int)socPct, (int)(error_state & 0xFF));

    // Update readable target chars
    setU16(pTVolt, (uint16_t)tVolt);
    setU16(pTAmp,  (uint16_t)tAmp);

    // Slow group (~5 s)
    static uint8_t loopCount = 0;
    if (++loopCount % 5 == 0) {
        setU16(pNomV,    (uint16_t)Config::getNominalVoltage());
        setU8 (pMaxMult, (uint8_t)(Config::getNominalMaxMultiplier() * 100));
        setU8 (pMinMult, (uint8_t)(Config::getNominalMinMultiplier() * 100));
        setU16(pAbsMaxV, (uint16_t)Config::getMaxVoltage());
        setU16(pAbsMinV, (uint16_t)Config::getMinVoltage());
        setU16(pCfgAmp,  (uint16_t)Config::getMaxCurrent());
        setU16(pCfgPct,  (uint16_t)(Config::getTargetPercentage() * 1000));
        setU16(pCfgTime, (uint16_t)Config::getMaxChargeTime());
    }
}
