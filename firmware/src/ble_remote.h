#pragma once

#if defined(GEEKCLOCK_SMALLTV_PRO)
#include <Arduino.h>
#include <NimBLEDevice.h>
// Arduino 3.3.11 releases unused controller memory before setup(). The external
// NimBLE host needs this marker to retain BLE memory; Classic remains unused.
#include <esp32-hal-alloc-ble-mem.h>
#include "ble_line_queue.h"

class BleRemote {
public:
    static constexpr const char* DEVICE_NAME = "Chiikawa-PRO";
    static constexpr const char* SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
    static constexpr const char* RX_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";
    static constexpr const char* TX_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";
    static constexpr size_t MAX_RESPONSE = 1023;

    BleRemote() : serverCallbacks_(*this), rxCallbacks_(*this), txCallbacks_(*this) {}

    bool begin() {
        if (ready_) return true;
        if (!NimBLEDevice::init(DEVICE_NAME)) return fail("ble_init_failed");
        NimBLEDevice::setMTU(185); // Still use 20-byte notifications for any phone MTU.
        NimBLEDevice::setSecurityAuth(false, false, false);
        server_ = NimBLEDevice::createServer();
        if (!server_) return fail("ble_server_failed");
        server_->setCallbacks(&serverCallbacks_, false);
        server_->advertiseOnDisconnect(false); // Main loop owns advertising retries.
        NimBLEService* service = server_->createService(SERVICE_UUID);
        if (!service) return fail("ble_service_failed");
        rx_ = service->createCharacteristic(RX_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR, 96);
        tx_ = service->createCharacteristic(TX_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY, 512);
        if (!rx_ || !tx_) return fail("ble_characteristic_failed");
        rx_->setCallbacks(&rxCallbacks_);
        tx_->setCallbacks(&txCallbacks_);
        tx_->setValue("{\"device\":\"Chiikawa-PRO\",\"protocol\":1,\"hint\":\"subscribe TX, then write status plus LF to RX\"}");
        if (!server_->start()) return fail("ble_service_start_failed");
        advertising_ = NimBLEDevice::getAdvertising();
        if (!advertising_) return fail("ble_advertising_failed");
        // The 128-bit service and full name do not fit together in 31 bytes.
        // Keep the service in advertisements and the full name in scan response.
        NimBLEAdvertisementData advertisement;
        advertisement.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
        advertisement.addServiceUUID(SERVICE_UUID);
        NimBLEAdvertisementData scanResponse;
        scanResponse.setName(DEVICE_NAME);
        if (!advertising_->setAdvertisementData(advertisement) ||
            !advertising_->setScanResponseData(scanResponse)) return fail("ble_advertisement_data_failed");
        advertising_->enableScanResponse(true);
        advertising_->setMinInterval(160); // 100–200 ms, with Wi-Fi coexistence.
        advertising_->setMaxInterval(320);
        ready_ = true;
        if (!advertising_->start()) lastError_ = "ble_advertising_failed";
        lastAdvertiseMs_ = millis();
        return true;
    }

    void loop(uint32_t nowMs) {
        if (!ready_) return;
        bool online, subscribed, busy;
        uint16_t handle;
        uint32_t session, rejected;
        portENTER_CRITICAL(&mux_);
        lines_.expire(nowMs);
        online = connected_;
        subscribed = subscribed_;
        busy = otaBusy_;
        handle = connectionHandle_;
        session = session_;
        rejected = lines_.rejected();
        portEXIT_CRITICAL(&mux_);
        if (session != observedSession_) {
            observedSession_ = session;
            responseLength_ = responseOffset_ = 0;
            sendFailedAtMs_ = 0;
            lastPeriodicMs_ = nowMs;
            reportedRejected_ = rejected;
        }
        if (!online) {
            if (!busy && !advertising_->isAdvertising() && uint32_t(nowMs - lastAdvertiseMs_) >= 1000) {
                lastAdvertiseMs_ = nowMs;
                if (!advertising_->start()) lastError_ = "ble_advertising_failed";
                else lastError_ = "";
            }
            return;
        }
        if (busy) return;
        if (uint32_t(nowMs - lastPeriodicMs_) >= 5000) {
            portENTER_CRITICAL(&mux_);
            statusRequested_ = true;
            portEXIT_CRITICAL(&mux_);
            lastPeriodicMs_ = nowMs;
        }
        if (rejected != reportedRejected_ && canSendResponse()) {
            if (sendResponse("{\"ok\":false,\"error\":\"invalid_or_full_rx\"}")) reportedRejected_ = rejected;
        }
        if (!responseLength_ || !subscribed || uint32_t(nowMs - lastNotifyMs_) < 20) return;
        const size_t remaining = responseLength_ - responseOffset_;
        const size_t length = remaining < 20 ? remaining : 20;
        lastNotifyMs_ = nowMs;
        if (tx_->notify(reinterpret_cast<const uint8_t*>(response_ + responseOffset_), length, handle)) {
            responseOffset_ += length;
            sendFailedAtMs_ = 0;
            if (responseOffset_ == responseLength_) responseLength_ = responseOffset_ = 0;
        } else {
            if (!sendFailedAtMs_) sendFailedAtMs_ = nowMs ? nowMs : 1;
            if (uint32_t(nowMs - sendFailedAtMs_) >= 3000) {
                lastError_ = "ble_notify_failed";
                // Never splice the next JSON into a partially sent frame.
                // A persistently failed link must reconnect with a clean session.
                server_->disconnect(handle);
            }
        }
    }

    bool popCommand(char* destination, size_t capacity) {
        if (!canSendResponse()) return false;
        portENTER_CRITICAL(&mux_);
        const bool accepted = connected_ && !otaBusy_ && lines_.pop(destination, capacity);
        portEXIT_CRITICAL(&mux_);
        return accepted;
    }

    bool sendResponse(const String& text) {
        if (!canSendResponse()) return false;
        const size_t length = text.length();
        if (!length || length > MAX_RESPONSE) {
            lastError_ = "ble_response_too_large";
            return false;
        }
        memcpy(response_, text.c_str(), length);
        response_[length] = '\n';
        response_[length + 1] = '\0';
        responseOffset_ = 0;
        responseLength_ = length + 1;
        sendFailedAtMs_ = 0;
        if (length <= 512) tx_->setValue(reinterpret_cast<const uint8_t*>(text.c_str()), length);
        else tx_->setValue("{\"hint\":\"subscribe TX notifications for the full JSON response\"}");
        return true;
    }

    bool canSendResponse() const {
        portENTER_CRITICAL(&mux_);
        const bool usable = connected_ && !otaBusy_ && session_ == observedSession_;
        portEXIT_CRITICAL(&mux_);
        return ready_ && usable && !responseLength_;
    }
    bool responsePending() const { return responseLength_ != 0; }
    bool ready() const { return ready_ && NimBLEDevice::isInitialized(); }
    bool connected() const {
        portENTER_CRITICAL(&mux_);
        const bool value = connected_;
        portEXIT_CRITICAL(&mux_);
        return value;
    }
    bool advertising() const { return ready_ && advertising_ && advertising_->isAdvertising(); }
    const char* lastError() const { return lastError_; }
    size_t queuedCommands() const {
        portENTER_CRITICAL(&mux_);
        const size_t count = lines_.count();
        portEXIT_CRITICAL(&mux_);
        return count;
    }
    uint32_t rejectedCommands() const {
        portENTER_CRITICAL(&mux_);
        const uint32_t count = lines_.rejected();
        portEXIT_CRITICAL(&mux_);
        return count;
    }
    bool takeStatusRequest() {
        if (!canSendResponse()) return false;
        portENTER_CRITICAL(&mux_);
        const bool requested = statusRequested_ && connected_ && !otaBusy_;
        statusRequested_ = false;
        portEXIT_CRITICAL(&mux_);
        return requested;
    }
    void setOtaBusy(bool busy) {
        portENTER_CRITICAL(&mux_);
        otaBusy_ = busy;
        lines_.reset();
        ++session_;
        statusRequested_ = !busy;
        portEXIT_CRITICAL(&mux_);
        responseLength_ = responseOffset_ = 0;
        if (ready_ && busy) advertising_->stop();
        if (!busy) lastAdvertiseMs_ = millis() - 1000;
    }

private:
    bool fail(const char* error) {
        lastError_ = error;
        ready_ = false;
        if (NimBLEDevice::isInitialized()) NimBLEDevice::deinit(true);
        server_ = nullptr;
        rx_ = tx_ = nullptr;
        advertising_ = nullptr;
        return false;
    }
    bool otaBusy() const {
        portENTER_CRITICAL(&mux_);
        const bool busy = otaBusy_;
        portEXIT_CRITICAL(&mux_);
        return busy;
    }

    class ServerCallbacks : public NimBLEServerCallbacks {
    public:
        explicit ServerCallbacks(BleRemote& owner) : owner_(owner) {}
        void onConnect(NimBLEServer* server, NimBLEConnInfo& info) override {
            portENTER_CRITICAL(&owner_.mux_);
            const bool reject = owner_.connected_ || owner_.otaBusy_;
            if (!reject) {
                owner_.connectionHandle_ = info.getConnHandle();
                owner_.connected_ = true;
                owner_.subscribed_ = false;
                owner_.lines_.reset();
                owner_.statusRequested_ = true;
                ++owner_.session_;
            }
            portEXIT_CRITICAL(&owner_.mux_);
            if (reject) server->disconnect(info.getConnHandle());
        }
        void onDisconnect(NimBLEServer*, NimBLEConnInfo& info, int) override {
            portENTER_CRITICAL(&owner_.mux_);
            if (owner_.connectionHandle_ == info.getConnHandle()) {
                owner_.connected_ = owner_.subscribed_ = false;
                owner_.connectionHandle_ = BLE_HS_CONN_HANDLE_NONE;
                owner_.lines_.reset();
                owner_.statusRequested_ = false;
                ++owner_.session_;
            }
            portEXIT_CRITICAL(&owner_.mux_);
        }
    private:
        BleRemote& owner_;
    };

    class RxCallbacks : public NimBLECharacteristicCallbacks {
    public:
        explicit RxCallbacks(BleRemote& owner) : owner_(owner) {}
        void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo& info) override {
            const auto value = characteristic->getValue();
            portENTER_CRITICAL(&owner_.mux_);
            if (owner_.connected_ && !owner_.otaBusy_ && owner_.connectionHandle_ == info.getConnHandle())
                owner_.lines_.append(value.data(), value.size(), millis());
            portEXIT_CRITICAL(&owner_.mux_);
        }
    private:
        BleRemote& owner_;
    };

    class TxCallbacks : public NimBLECharacteristicCallbacks {
    public:
        explicit TxCallbacks(BleRemote& owner) : owner_(owner) {}
        void onSubscribe(NimBLECharacteristic*, NimBLEConnInfo& info, uint16_t value) override {
            portENTER_CRITICAL(&owner_.mux_);
            if (owner_.connectionHandle_ == info.getConnHandle()) {
                owner_.subscribed_ = (value & 1) != 0;
                owner_.statusRequested_ = true;
            }
            portEXIT_CRITICAL(&owner_.mux_);
        }
    private:
        BleRemote& owner_;
    };

    mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
    BleLineQueue lines_;
    bool connected_ = false, subscribed_ = false, otaBusy_ = false, statusRequested_ = false;
    uint16_t connectionHandle_ = BLE_HS_CONN_HANDLE_NONE;
    uint32_t session_ = 0;
    bool ready_ = false;
    const char* lastError_ = "";
    NimBLEServer* server_ = nullptr;
    NimBLECharacteristic* rx_ = nullptr;
    NimBLECharacteristic* tx_ = nullptr;
    NimBLEAdvertising* advertising_ = nullptr;
    char response_[MAX_RESPONSE + 2] = {};
    size_t responseLength_ = 0, responseOffset_ = 0;
    uint32_t observedSession_ = 0, reportedRejected_ = 0;
    uint32_t lastAdvertiseMs_ = 0, lastNotifyMs_ = 0, lastPeriodicMs_ = 0, sendFailedAtMs_ = 0;
    ServerCallbacks serverCallbacks_;
    RxCallbacks rxCallbacks_;
    TxCallbacks txCallbacks_;
};
#endif
