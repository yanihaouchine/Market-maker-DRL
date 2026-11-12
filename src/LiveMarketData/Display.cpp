#include "LiveMarketData/Display.h"

#include <algorithm>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

using json = nlohmann::json;

namespace {
void EnsureDirectoryExistsForFile(const std::string &filename) {
    size_t lastSlash = filename.find_last_of("/\\");
    if (lastSlash == std::string::npos) {
        return;
    }

    std::string dir = filename.substr(0, lastSlash);
    if (dir.empty()) {
        return;
    }

    std::replace(dir.begin(), dir.end(), '\\', '/');

    std::stringstream ss(dir);
    std::string part;
    std::string current;

    if (!dir.empty() && dir[0] == '/') {
        current = "/";
    }

    while (std::getline(ss, part, '/')) {
        if (part.empty()) {
            continue;
        }

        if (!current.empty() && current.back() != '/') {
            current += '/';
        }
        current += part;

#ifdef _WIN32
        _mkdir(current.c_str());
#else
        mkdir(current.c_str(), 0755);
#endif
    }
}
}  // namespace

std::string FormatTimestamp(const std::chrono::system_clock::time_point &tp) {
    auto time = std::chrono::system_clock::to_time_t(tp);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

void PrintOrderbook(const Orderbook &orderbook, const std::string &symbol, int levels) {
    auto infos = orderbook.GetOrderInfos();
    const auto &bids = infos.GetBids();
    const auto &asks = infos.GetAsks();

#ifdef _WIN32
    system("cls");
#else
    // Séquence ANSI « effacer l'écran, curseur en haut à gauche » : évite de lancer un shell
    // à chaque rafraîchissement (mesuré : 1,7 ms pour system("clear") contre 0,4 µs).
    std::cout << "\033[2J\033[H";
#endif

    auto now = std::chrono::system_clock::now();

    std::cout << "========================================\n";
    std::cout << "  LIVE ORDERBOOK: " << symbol << "\n";
    std::cout << "  " << FormatTimestamp(now) << "\n";
    std::cout << "========================================\n\n";

    std::cout << std::setw(15) << "BID QTY" << " | "
              << std::setw(12) << "BID PRICE" << " | "
              << std::setw(12) << "ASK PRICE" << " | "
              << std::setw(15) << "ASK QTY" << "\n";
    std::cout << std::string(65, '-') << "\n";

    int maxLevels = std::max(std::min((int) bids.size(), levels),
                             std::min((int) asks.size(), levels));

    for (int i = 0; i < maxLevels; ++i) {
        if (i < bids.size()) {
            std::cout << std::setw(15) << std::fixed << std::setprecision(2)
                      << bids[i].quantity_ / 100.0 << " | "
                      << std::setw(12) << std::fixed << std::setprecision(2)
                      << bids[i].price_ / 100.0 << " | ";
        } else {
            std::cout << std::setw(15) << "-" << " | "
                      << std::setw(12) << "-" << " | ";
        }

        if (i < asks.size()) {
            std::cout << std::setw(12) << std::fixed << std::setprecision(2)
                      << asks[i].price_ / 100.0 << " | "
                      << std::setw(15) << std::fixed << std::setprecision(2)
                      << asks[i].quantity_ / 100.0 << "\n";
        } else {
            std::cout << std::setw(12) << "-" << " | "
                      << std::setw(15) << "-" << "\n";
        }
    }

    std::cout << "========================================\n";

    if (!bids.empty() && !asks.empty()) {
        double spread = (asks[0].price_ - bids[0].price_) / 100.0;
        double midPrice = (bids[0].price_ + asks[0].price_) / 200.0;
        double spreadBps = (spread / midPrice) * 10000;

        std::cout << "Best Bid: $" << std::fixed << std::setprecision(2)
                  << bids[0].price_ / 100.0 << "\n";
        std::cout << "Best Ask: $" << asks[0].price_ / 100.0 << "\n";
        std::cout << "Spread: $" << spread << " (" << std::setprecision(1)
                  << spreadBps << " bps)\n";
        std::cout << "Mid Price: $" << std::setprecision(2) << midPrice << "\n";
    }

    std::cout << "\nOrderbook Size: " << orderbook.Size() << " orders\n";

    const auto &stats = orderbook.GetMarketDataStats();
    std::cout << "Messages Processed: " << stats.messagesProcessed << "\n";
    std::cout << "Average Latency: " << std::fixed << std::setprecision(3)
              << stats.GetAverageLatencyMicros() << " μs\n";

    std::cout << "========================================\n";
    std::cout << "\nPress Ctrl+C to exit...\n";
}

void WriteMarketStateJson(const Orderbook &orderbook, const std::string &filename, int iteration, const std::string &symbol) {
    try {
        auto infos = orderbook.GetOrderInfos();
        const auto &bids = infos.GetBids();
        const auto &asks = infos.GetAsks();

        json state;
        
        state["symbol"] = symbol;
        
        if (!bids.empty() && !asks.empty()) {
            state["bid_price"] = bids[0].price_;
            state["ask_price"] = asks[0].price_;
            
            double spread = (asks[0].price_ - bids[0].price_) / 100.0;
            double midPrice = (bids[0].price_ + asks[0].price_) / 200.0;
            double spreadBps = (spread / midPrice) * 10000;
            
            state["mid_price"] = (bids[0].price_ + asks[0].price_) / 2.0;
            state["spread"] = asks[0].price_ - bids[0].price_;
            state["spread_bps"] = spreadBps;
        }

        state["iteration"] = iteration;

        json bids_arr = json::array();
        for (const auto &bid : bids) {
            bids_arr.push_back({
                {"price", bid.price_},
                {"quantity", bid.quantity_}
            });
        }
        state["bids"] = bids_arr;

        json asks_arr = json::array();
        for (const auto &ask : asks) {
            asks_arr.push_back({
                {"price", ask.price_},
                {"quantity", ask.quantity_}
            });
        }
        state["asks"] = asks_arr;

        EnsureDirectoryExistsForFile(filename);

        std::ofstream file(filename);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open file for writing: " + filename);
        }
        file << state.dump(2);
        file.close();
    } catch (const std::exception &e) {
        std::cerr << "Error writing market state: " << e.what() << "\n";
    }
}
