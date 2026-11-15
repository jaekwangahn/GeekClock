#define EXCHANGE_RATE_HOST_TEST
#include "../src/exchange_rate.h"

#include <assert.h>
#include <iostream>
#include <string>

static bool parse(const std::string& input, exchange_detail::ParsedRate* out = nullptr) {
    exchange_detail::ParsedRate result;
    const bool ok = exchange_detail::parseResponse(input.data(), input.size(), result);
    if (ok && out) *out = result;
    return ok;
}

int main() {
    const std::string response = "{\"date\":\"2026-11-13\",\"base\":\"JPY\",\"quote\":\"KRW\",\"rate\":8.3572}";
    exchange_detail::ParsedRate rate;
    assert(parse(response, &rate));
    assert(fabs(rate.rate * 100 - 835.72) < 0.0000001);
    assert(strcmp(rate.date, "2026-11-13") == 0);
    assert(parse(" \n{\"rate\":8.3572e0, \"quote\":\"KRW\",\"date\":\"2024-02-29\",\"base\":\"JPY\"}\r\n"));
    assert(exchange_detail::validDate("2000-02-29"));
    assert(!exchange_detail::validDate("2100-02-29"));
    assert(!exchange_detail::validDate("2026-02-29"));
    assert(!exchange_detail::validDate("2026-04-31"));
    assert(!exchange_detail::validDate("2026-00-01"));
    assert(!exchange_detail::validDate("2026-01-00"));
    assert(!exchange_detail::validDate("2026-1-01"));

    for (size_t i = 0; i < response.size(); ++i) assert(!parse(response.substr(0, i)));
    assert(!parse(response + "garbage"));
    assert(!parse(response + "{}"));
    assert(!parse(response + std::string(1, '\0') + "garbage"));
    assert(!parse("{"));
    assert(!parse("{}"));
    assert(!parse("null"));
    assert(!parse("[]"));

    const char* bad[] = {
        "{\"date\":\"2026-11-13\",\"base\":\"USD\",\"quote\":\"KRW\",\"rate\":8}",
        "{\"date\":\"2026-11-13\",\"base\":\"JPY\",\"quote\":\"USD\",\"rate\":8}",
        "{\"date\":\"2026-11-13\",\"base\":\"JPY\",\"quote\":\"KRW\",\"rate\":8,\"rate\":9}",
        "{\"date\":\"2026-11-13\",\"base\":\"JPY\",\"quote\":\"KRW\",\"rate\":8,\"extra\":1}",
        "{\"date\":\"2026-11-13\",\"base\":\"JPY\",\"quote\":\"KRW\",\"rate\":\"8\"}",
        "{\"date\":\"2026-11-13\",\"base\":\"JPY\",\"quote\":\"KRW\",\"rate\":NaN}",
        "{\"date\":\"2026-11-13\",\"base\":\"JPY\",\"quote\":\"KRW\",\"rate\":null}",
        "{\"date\":\"2026-11-13\",\"base\":\"JPY\",\"quote\":\"KRW\",\"rate\":0}",
        "{\"date\":\"2026-11-13\",\"base\":\"JPY\",\"quote\":\"KRW\",\"rate\":-8}",
        "{\"date\":\"2026-11-13\",\"base\":\"JPY\",\"quote\":\"KRW\",\"rate\":1001}",
        "{\"date\":\"2026-11-13\",\"base\":\"JPY\",\"quote\":\"KRW\",\"rate\":1e9999}",
        "{\"date\":\"2026-11-13\",\"base\":\"JPY\",\"quote\":\"KRW\",\"rate\":08}",
        "{\"date\":\"2026-11-13\",\"base\":\"JPY\",\"quote\":\"KRW\",\"rate\":8.}",
        "{\"date\":\"2026-11-13\",\"base\":\"JPY\",\"quote\":\"KRW\",\"rate\":8e}",
        "{\"date\":\"2026-11-13\",\"base\":\"JPY\",\"quote\":\"KRW\",\"rate\":8,}",
        "{\"date\":\"2026-02-30\",\"base\":\"JPY\",\"quote\":\"KRW\",\"rate\":8}",
        "{\"date\":\"2026-11-13\",\"base\":\"JP\\u0059\",\"quote\":\"KRW\",\"rate\":8}"
    };
    for (const char* input : bad) assert(!parse(input));
    assert(!parse(std::string(1025, ' ')));
    std::cout << "PASS: exchange JSON/date/range/truncation/currency validation and 100JPY conversion\n";
}
