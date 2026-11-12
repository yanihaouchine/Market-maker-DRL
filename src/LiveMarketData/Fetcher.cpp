#include "LiveMarketData/Fetcher.h"

#include <curl/curl.h>
#include <iostream>
#include <string>

namespace {
size_t WriteCallback(void *contents, size_t size, size_t nmemb, std::string *userp) {
    size_t totalSize = size * nmemb;
    userp->append((char *) contents, totalSize);
    return totalSize;
}

// Une seule poignée curl pour toute la durée du processus : la connexion TCP et la session TLS
// vers Binance restent ouvertes d'une requête à l'autre, au lieu d'être renégociées à chaque
// itération (mesuré : ~400 ms par requête au lieu de 600 à 1500 ms). La poignée vit jusqu'à la
// fin du processus et n'est volontairement pas libérée.
CURL *PersistentHandle() {
    static CURL *const curl = [] {
        CURL *handle = curl_easy_init();
        if (handle) {
            curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, WriteCallback);
            curl_easy_setopt(handle, CURLOPT_TIMEOUT, 10L);
            curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 1L);
            curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 2L);
            curl_easy_setopt(handle, CURLOPT_USERAGENT, "Mozilla/5.0");
            curl_easy_setopt(handle, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
            curl_easy_setopt(handle, CURLOPT_TCP_KEEPALIVE, 1L);
            curl_easy_setopt(handle, CURLOPT_TCP_NODELAY, 1L);
        }
        return handle;
    }();
    return curl;
}
} // namespace

std::string FetchBinanceOrderbook(const std::string &symbol, int limit) {
    std::string readBuffer;

    CURL *curl = PersistentHandle();
    if (curl) {
        std::string url = "https://api.binance.com/api/v3/depth?symbol=" + symbol +
                          "&limit=" + std::to_string(limit);

        readBuffer.reserve(16 * 1024);
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

        CURLcode res = curl_easy_perform(curl);

        if (res != CURLE_OK) {
            std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
        }
    }

    return readBuffer;
}
