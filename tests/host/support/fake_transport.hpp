#pragma once
// Skriptbarer HTTP-Transport für Host-Tests (M27): erwartete Anfragen in Reihenfolge, jede mit fester Antwort.
// Eine unerwartete Anfrage (falsche Methode, falscher URL-Anfang oder Skript zu Ende) bekommt Status 599.

#include <string>
#include <utility>
#include <vector>

#include "core/http_types.hpp"

namespace nl::test {

inline HttpResponse jsonResponse(u16 status, std::string_view body) {
    HttpResponse r;
    r.status = status;
    r.error = NetError::None;
    r.headers.emplace_back("Content-Type", "application/json");
    r.body.assign(body.begin(), body.end());
    return r;
}

inline HttpResponse offlineResponse() {
    HttpResponse r;
    r.status = 0;
    r.error = NetError::Connect;
    return r;
}

class FakeTransport final : public HttpTransport {
public:
    struct Step {
        HttpMethod method;
        std::string urlPrefix;
        HttpResponse response;
    };

    void expect(HttpMethod m, std::string urlPrefix, HttpResponse r) {
        steps_.push_back(Step{m, std::move(urlPrefix), std::move(r)});
    }

    HttpResponse send(const HttpRequest& req, const volatile bool& /*cancel*/) override {
        seen.push_back(req);
        if (next_ >= steps_.size()) return unexpected();
        const Step& s = steps_[next_];
        if (s.method != req.method || req.url.rfind(s.urlPrefix, 0) != 0) return unexpected();
        ++next_;
        return s.response;
    }

    bool done() const { return next_ == steps_.size(); }

    std::vector<HttpRequest> seen;

private:
    static HttpResponse unexpected() {
        HttpResponse r;
        r.status = 599;
        r.error = NetError::Http;
        return r;
    }

    std::vector<Step> steps_;
    std::size_t next_ = 0;
};

}  // namespace nl::test
