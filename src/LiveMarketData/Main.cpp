#include <chrono>
#include <curl/curl.h>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>

#include "LiveMarketData/Display.h"
#include "LiveMarketData/Fetcher.h"
#include "LiveMarketData/Parser.h"
#include "OrderBook/OrderBook.h"

int main(int argc, char *argv[]) {
    curl_global_init(CURL_GLOBAL_DEFAULT);

    // Default to SOL/USDT, but allow command line override
    std::string symbol = "SOLUSDT";
    int refreshInterval = 1; // seconds
    int displayLevels = 50;

    if (argc > 1) {
        symbol = argv[1];
    }
    if (argc > 2) {
        refreshInterval = std::stoi(argv[2]);
    }
    if (argc > 3) {
        displayLevels = std::stoi(argv[3]);
    }

    std::cout << "========================================\n";
    std::cout << "  Binance Live Market Data Feed\n";
    std::cout << "========================================\n";
    std::cout << "Symbol: " << symbol << "\n";
    std::cout << "Refresh Interval: " << refreshInterval << " seconds\n";
    std::cout << "Display Levels: " << displayLevels << "\n";
    std::cout << "\nConnecting to Binance API...\n\n";
    std::cout << "Usage: ./LiveMarketData [SYMBOL] [REFRESH_SECONDS] [LEVELS]\n";
    std::cout << "Example: ./LiveMarketData ETHUSDT 1 15\n\n";

    Orderbook orderbook;

    try {
        int iteration = 0;
        while (true) {
            iteration++;
            std::string jsonResponse = FetchBinanceOrderbook(symbol, displayLevels);

            if (jsonResponse.empty()) {
                std::cerr << "Failed to fetch orderbook data\n";
                std::this_thread::sleep_for(std::chrono::seconds(refreshInterval));
                continue;
            }

            try {
                BookSnapshotMessage snapshot = ParseBinanceSnapshot(jsonResponse);

                if (orderbook.ProcessMarketData(snapshot)) {
                    WriteMarketStateJson(orderbook, "../data/market_state.json", iteration, symbol);

                    PrintOrderbook(orderbook, symbol, displayLevels);
                } else {
                    std::cerr << "Failed to process market data\n";
                }
            } catch (const nlohmann::json::exception &e) {
                std::cerr << "JSON parsing error: " << e.what() << "\n";
                std::cerr << "Response: " << jsonResponse.substr(0, 200) << "...\n";
            } catch (const std::exception &e) {
                std::cerr << "Error processing data: " << e.what() << "\n";
            }

            std::this_thread::sleep_for(std::chrono::seconds(refreshInterval));
        }
    } catch (const std::exception &e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        curl_global_cleanup();
        return 1;
    }

    curl_global_cleanup();

    return 0;
}
