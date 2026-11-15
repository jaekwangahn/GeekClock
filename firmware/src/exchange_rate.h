#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>

struct ExchangeSnapshot {
    bool valid = false;
    bool loading = false;
    bool stale = true;
    double krwPerJpy = 0;
    char rateDate[11] = {};
    uint32_t fetchedEpoch = 0;
    uint32_t fetchedMillis = 0;
    uint32_t revision = 0;
    char error[72] = {};
};

namespace exchange_detail {
struct ParsedRate {
    double rate = 0;
    char date[11] = {};
};

inline bool validDate(const char* date) {
    if (strlen(date) != 10 || date[4] != '-' || date[7] != '-') return false;
    for (unsigned i = 0; i < 10; ++i)
        if (i != 4 && i != 7 && (date[i] < '0' || date[i] > '9')) return false;
    const int year = (date[0]-'0')*1000+(date[1]-'0')*100+(date[2]-'0')*10+date[3]-'0';
    const int month = (date[5]-'0')*10+date[6]-'0';
    const int day = (date[8]-'0')*10+date[9]-'0';
    if (year < 2000 || year > 2100 || month < 1 || month > 12 || day < 1) return false;
    static const uint8_t days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    const bool leap = year%4 == 0 && (year%100 != 0 || year%400 == 0);
    return day <= days[month-1] + (month == 2 && leap ? 1 : 0);
}

// The provider's pair endpoint returns one flat object. Parse its complete JSON
// grammar (not substring matches), rejecting ambiguity, trailing data and NaN.
class Parser {
public:
    Parser(const char* data, size_t size) : at_(data), end_(data + size) {}
    bool parse(ParsedRate& result) {
        if (!take('{')) return false;
        uint8_t fields = 0;
        ParsedRate parsed;
        while (true) {
            char key[12];
            if (!string(key, sizeof(key)) || !take(':')) return false;
            uint8_t bit = 0;
            if (strcmp(key, "date") == 0) {
                bit = 1;
                if (!string(parsed.date, sizeof(parsed.date)) || !validDate(parsed.date)) return false;
            } else if (strcmp(key, "base") == 0 || strcmp(key, "quote") == 0) {
                char value[4];
                bit = key[0] == 'b' ? 2 : 4;
                if (!string(value, sizeof(value)) || strcmp(value, bit == 2 ? "JPY" : "KRW") != 0) return false;
            } else if (strcmp(key, "rate") == 0) {
                bit = 8;
                if (!number(parsed.rate)) return false;
            } else return false;
            if (fields & bit) return false;
            fields |= bit;
            space();
            if (at_ < end_ && *at_ == '}') { ++at_; break; }
            if (!take(',')) return false;
        }
        space();
        if (at_ != end_ || fields != 15 || !isfinite(parsed.rate) || parsed.rate < 0.01 || parsed.rate > 1000) return false;
        result = parsed;
        return true;
    }
private:
    const char* at_;
    const char* end_;
    void space() { while (at_ < end_ && (*at_ == ' ' || *at_ == '\t' || *at_ == '\r' || *at_ == '\n')) ++at_; }
    bool take(char value) { space(); if (at_ == end_ || *at_ != value) return false; ++at_; return true; }
    bool string(char* out, size_t capacity) {
        if (!take('"')) return false;
        size_t n = 0;
        while (at_ < end_ && *at_ != '"') {
            // Currency codes, key names and dates never require JSON escapes.
            if (*at_ == '\\' || static_cast<unsigned char>(*at_) < 0x20 || n+1 >= capacity) return false;
            out[n++] = *at_++;
        }
        if (at_ == end_) return false;
        ++at_; out[n] = 0; return true;
    }
    bool number(double& result) {
        space();
        const char* start = at_;
        if (at_ < end_ && *at_ == '-') ++at_;
        if (at_ == end_ || *at_ < '0' || *at_ > '9') return false;
        if (*at_ == '0') ++at_;
        else while (at_ < end_ && *at_ >= '0' && *at_ <= '9') ++at_;
        if (at_ < end_ && *at_ == '.') {
            ++at_;
            const char* fraction = at_;
            while (at_ < end_ && *at_ >= '0' && *at_ <= '9') ++at_;
            if (at_ == fraction) return false;
        }
        if (at_ < end_ && (*at_ == 'e' || *at_ == 'E')) {
            ++at_;
            if (at_ < end_ && (*at_ == '+' || *at_ == '-')) ++at_;
            const char* exponent = at_;
            while (at_ < end_ && *at_ >= '0' && *at_ <= '9') ++at_;
            if (at_ == exponent) return false;
        }
        const size_t size = size_t(at_ - start);
        if (size >= 40) return false;
        char token[40]; memcpy(token, start, size); token[size] = 0;
        char* end = nullptr; result = strtod(token, &end);
        return end == token + size && isfinite(result);
    }
};

inline bool parseResponse(const char* body, size_t size, ParsedRate& result) {
    return size > 0 && size <= 1024 && Parser(body, size).parse(result);
}
} // namespace exchange_detail

#ifndef EXCHANGE_RATE_HOST_TEST
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// api.frankfurter.dev currently uses Google Trust Services WE1 -> GTS Root R4.
// Official root: https://pki.goog/repo/certs/gtsr4.pem (expires 2036-06-22).
// Verify the TLS hostname and certificate; never fall back to insecure HTTP.
static const char EXCHANGE_GTS_ROOT_R4[] PROGMEM = R"CERT(-----BEGIN CERTIFICATE-----
MIICCTCCAY6gAwIBAgINAgPlwGjvYxqccpBQUjAKBggqhkjOPQQDAzBHMQswCQYD
VQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2VzIExMQzEUMBIG
A1UEAxMLR1RTIFJvb3QgUjQwHhcNMTYwNjIyMDAwMDAwWhcNMzYwNjIyMDAwMDAw
WjBHMQswCQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2Vz
IExMQzEUMBIGA1UEAxMLR1RTIFJvb3QgUjQwdjAQBgcqhkjOPQIBBgUrgQQAIgNi
AATzdHOnaItgrkO4NcWBMHtLSZ37wWHO5t5GvWvVYRg1rkDdc/eJkTBa6zzuhXyi
QHY7qca4R9gq55KRanPpsXI5nymfopjTX15YhmUPoYRlBtHci8nHc8iMai/lxKvR
HYqjQjBAMA4GA1UdDwEB/wQEAwIBhjAPBgNVHRMBAf8EBTADAQH/MB0GA1UdDgQW
BBSATNbrdP9JNqPV2Py1PsVq8JQdjDAKBggqhkjOPQQDAwNpADBmAjEA6ED/g94D
9J+uHXqnLrmvT/aDHQ4thQEd0dlq7A/Cr8deVl5c1RxYIigL9zC2L7F8AjEA8GE8
p/SgguMh1YQdc4acLa/KNJvxn7kjNuK8YAOdgLOaVsjh4rsUecrNIdSUtUlD
-----END CERTIFICATE-----
)CERT";

class ExchangeRateClient {
public:
    bool begin() {
        if (task_) return true;
        Preferences prefs;
        if (prefs.begin("chiifx", true)) {
            const double rate = prefs.getDouble("rate", 0);
            const String date = prefs.getString("date", "");
            if (isfinite(rate) && rate >= 0.01 && rate <= 1000 && exchange_detail::validDate(date.c_str())) {
                state_.valid = true;
                state_.krwPerJpy = rate;
                memcpy(state_.rateDate, date.c_str(), 11);
                state_.fetchedEpoch = prefs.getUInt("fetched", 0);
            }
            prefs.end();
        }
        // Cached quotes are explicitly stale until the first verified request.
        state_.stale = true;
        if (xTaskCreate(workerEntry, "jpy-rate", 8192, this, 1, &task_) != pdPASS) {
            task_ = nullptr;
            setWaitingError("Exchange worker unavailable");
            return false;
        }
        return true;
    }

    ExchangeSnapshot snapshot() {
        portENTER_CRITICAL(&lock_);
        ExchangeSnapshot result = state_;
        portEXIT_CRITICAL(&lock_);
        const time_t now = time(nullptr);
        if (result.valid && (now < 1700000000 || result.fetchedEpoch == 0 ||
            now < time_t(result.fetchedEpoch) || uint64_t(now) - result.fetchedEpoch >= 86400 ||
            WiFi.status() != WL_CONNECTED)) result.stale = true;
        return result;
    }

    void service() { request(false); }

    bool request(bool manual = false) {
        if (!task_) return false;
        ExchangeSnapshot before = snapshot();
        if (before.loading) return false;
        const uint32_t now = millis();
        const uint32_t interval = manual ? 30000UL :
                                  (before.valid && before.error[0] == 0 ? 900000UL : 60000UL);
        if (attempted_ && uint32_t(now - lastAttemptMs_) < interval) return false;
        if (WiFi.status() != WL_CONNECTED) { setWaitingError("Waiting for Wi-Fi"); return false; }
        if (time(nullptr) < 1700000000) { setWaitingError("Waiting for clock synchronization"); return false; }
        lastAttemptMs_ = now;
        attempted_ = true;
        portENTER_CRITICAL(&lock_);
        state_.loading = true;
        ++state_.revision;
        portEXIT_CRITICAL(&lock_);
        xTaskNotifyGive(task_);
        return true;
    }

private:
    portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
    TaskHandle_t task_ = nullptr;
    ExchangeSnapshot state_;
    uint32_t lastAttemptMs_ = 0;
    bool attempted_ = false;

    void setWaitingError(const char* message) {
        portENTER_CRITICAL(&lock_);
        if (strcmp(state_.error, message) != 0) {
            snprintf(state_.error, sizeof(state_.error), "%s", message);
            state_.stale = true;
            ++state_.revision;
        }
        portEXIT_CRITICAL(&lock_);
    }

    static void workerEntry(void* context) {
        ExchangeRateClient* self = static_cast<ExchangeRateClient*>(context);
        while (true) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            self->fetch();
        }
    }

    void finishFailure(const char* message) {
        portENTER_CRITICAL(&lock_);
        snprintf(state_.error, sizeof(state_.error), "%s", message);
        state_.loading = false;
        state_.stale = true;
        ++state_.revision;
        portEXIT_CRITICAL(&lock_);
    }

    bool readBody(HTTPClient& http, char* body, size_t& size) {
        const int expected = http.getSize();
        if (expected > 1024) return false;
        NetworkClient* stream = http.getStreamPtr();
        if (!stream) return false;
        const uint32_t started = millis();
        size = 0;
        while (uint32_t(millis() - started) < 5000) {
            const int available = stream->available();
            if (available > 0) {
                if (size == 1024) return false;
                const size_t count = min(size_t(available), size_t(1024 - size));
                const int received = stream->read(reinterpret_cast<uint8_t*>(body + size), count);
                if (received < 0) return false;
                size += size_t(received);
                if (expected >= 0 && size >= size_t(expected)) break;
            } else if (!stream->connected()) break;
            else vTaskDelay(pdMS_TO_TICKS(10));
        }
        body[size] = 0;
        return size > 0 && (expected < 0 ? !stream->connected() : size == size_t(expected));
    }

    void fetch() {
        WiFiClientSecure client;
        client.setCACert(EXCHANGE_GTS_ROOT_R4);
        client.setHandshakeTimeout(5);
        client.setTimeout(5000);
        HTTPClient http;
        http.setConnectTimeout(5000);
        http.setTimeout(5000);
        http.useHTTP10(true);
        http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
        if (!http.begin(client, "https://api.frankfurter.dev/v2/providers/ecb/rate/jpy/krw")) {
            finishFailure("Unable to start HTTPS request"); return;
        }
        const int status = http.GET();
        if (status != 200) {
            char error[72]; snprintf(error, sizeof(error), "Exchange HTTPS failed (%d)", status);
            http.end(); finishFailure(error); return;
        }
        char body[1025]; size_t size = 0;
        const bool complete = readBody(http, body, size);
        http.end();
        exchange_detail::ParsedRate parsed;
        if (!complete || !exchange_detail::parseResponse(body, size, parsed)) {
            finishFailure("Invalid or incomplete exchange response"); return;
        }
        // Provider dates use UTC calendar days; tolerate one day for timezone
        // boundaries, but never accept arbitrary future or malformed quotes.
        const time_t now = time(nullptr);
        struct tm tomorrow;
        const time_t tomorrowEpoch = now + 86400;
        gmtime_r(&tomorrowEpoch, &tomorrow);
        char latestAllowed[11]; strftime(latestAllowed, sizeof(latestAllowed), "%Y-%m-%d", &tomorrow);
        if (strcmp(parsed.date, latestAllowed) > 0) {
            finishFailure("Exchange date is in the future"); return;
        }
        Preferences prefs;
        if (prefs.begin("chiifx", false)) {
            prefs.putDouble("rate", parsed.rate);
            prefs.putString("date", parsed.date);
            prefs.putUInt("fetched", uint32_t(now));
            prefs.end();
        }
        portENTER_CRITICAL(&lock_);
        state_.valid = true;
        state_.loading = false;
        state_.stale = false;
        state_.krwPerJpy = parsed.rate;
        memcpy(state_.rateDate, parsed.date, sizeof(state_.rateDate));
        state_.fetchedEpoch = uint32_t(now);
        state_.fetchedMillis = millis();
        state_.error[0] = 0;
        ++state_.revision;
        portEXIT_CRITICAL(&lock_);
    }
};
#endif // EXCHANGE_RATE_HOST_TEST
