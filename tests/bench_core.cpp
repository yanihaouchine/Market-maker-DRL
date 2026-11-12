// Banc de mesure du moteur de matching.
//
// Les flux d'opérations sont générés à l'avance (hors chronométrage) puis rejoués d'un bloc ;
// on mesure le bloc entier (horloge monotone + compteur TSC), pas chaque opération : le coût
// d'une lecture d'horloge est du même ordre que celui d'une opération.
//
// Usage : OrderBookBenchCore [--engine=nom] [--scenario=nom] [--orders=N] [--levels=L] [--reps=R]
//                            [--random-ids]
// Sans argument : toutes les combinaisons. Lancer avec `taskset -c 1` (et `perf stat`) pour des
// chiffres stables.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include <x86intrin.h>

#include "OrderBook/BookCore.h"
#include "OrderBook/OrderBook.h"
#include "reference/RefOrderBook.h"

namespace {
enum class OpKind : std::uint8_t { Add, Cancel, Modify };

struct Op {
    OrderId id;
    Price price;
    Quantity quantity;
    OpKind kind;
    Side side;
    OrderType type;
};

struct Workload {
    std::vector<Op> setup; // rejoué hors chronométrage
    std::vector<Op> timed;
};

// ---------------------------------------------------------------------------------------------
// Moteurs
// ---------------------------------------------------------------------------------------------

// Implémentation d'origine (tests/reference).
struct RefEngine {
    ref::Orderbook book;
    std::uint64_t traded = 0;

    void Reserve(std::size_t) {
    }

    void Apply(const Op &op) {
        switch (op.kind) {
            case OpKind::Add: {
                const auto trades = book.AddOrder(
                    std::make_shared<ref::Order>(op.type, op.id, op.side, op.price, op.quantity));
                for (const Trade &t: trades) traded += t.GetBidTrade().quantity_;
                break;
            }
            case OpKind::Cancel:
                book.CancelOrder(op.id);
                break;
            case OpKind::Modify: {
                const auto trades = book.MatchOrder(ref::OrderModify(op.id, op.side, op.price, op.quantity));
                for (const Trade &t: trades) traded += t.GetBidTrade().quantity_;
                break;
            }
        }
    }

    std::size_t Size() const { return book.Size(); }
};

// Moteur optimisé à travers l'API de compatibilité (shared_ptr<Order>, Trades par valeur).
struct CompatEngine {
    Orderbook book;
    std::uint64_t traded = 0;

    void Reserve(std::size_t n) { book.Reserve(n); }

    void Apply(const Op &op) {
        switch (op.kind) {
            case OpKind::Add: {
                const auto trades = book.AddOrder(
                    std::make_shared<Order>(op.type, op.id, op.side, op.price, op.quantity));
                for (const Trade &t: trades) traded += t.GetBidTrade().quantity_;
                break;
            }
            case OpKind::Cancel:
                book.CancelOrder(op.id);
                break;
            case OpKind::Modify: {
                const auto trades = book.MatchOrder(OrderModify(op.id, op.side, op.price, op.quantity));
                for (const Trade &t: trades) traded += t.GetBidTrade().quantity_;
                break;
            }
        }
    }

    std::size_t Size() const { return book.Size(); }
};

// Cœur du moteur par l'API rapide, paramétré par la structure de niveaux et la table d'ids.
template<class Levels, class Ids = ob::IdTable<>>
struct CoreEngine {
    ob::BookCore<Levels, Ids> book;
    Trades trades;
    std::uint64_t traded = 0;

    void Reserve(std::size_t n) { book.Reserve(n); }

    void Apply(const Op &op) {
        switch (op.kind) {
            case OpKind::Add:
                trades.clear();
                book.Add(op.type, op.id, op.side, op.price, op.quantity, trades);
                for (const Trade &t: trades) traded += t.GetBidTrade().quantity_;
                break;
            case OpKind::Cancel:
                book.Cancel(op.id);
                break;
            case OpKind::Modify:
                trades.clear();
                book.Modify(op.id, op.side, op.price, op.quantity, trades);
                for (const Trade &t: trades) traded += t.GetBidTrade().quantity_;
                break;
        }
    }

    std::size_t Size() const { return book.Size(); }
};

// ---------------------------------------------------------------------------------------------
// Flux d'opérations
// ---------------------------------------------------------------------------------------------

constexpr Price kMid = 1000000;

// Distance au meilleur prix : concentrée près du haut du carnet, bornée par le nombre de niveaux.
int Depth(std::mt19937_64 &gen, int levels) {
    const double u = (static_cast<double>(gen() >> 11) + 1.0) / 9007199254740993.0;
    const int d = static_cast<int>(-std::log(u) * levels / 5.0);
    return d < levels ? d : levels - 1;
}

Op Passive(std::mt19937_64 &gen, OrderId id, int levels) {
    const Side side = (gen() & 1) ? Side::Buy : Side::Sell;
    const int d = Depth(gen, levels);
    const Price price = side == Side::Buy ? kMid - 1 - d : kMid + 1 + d;
    return Op{id, price, static_cast<Quantity>(1 + gen() % 100), OpKind::Add, side, OrderType::GoodTillCancel};
}

// N ordres passifs chronométrés.
Workload MakeAdd(int orders, int levels) {
    std::mt19937_64 gen(1);
    Workload w;
    for (int i = 0; i < orders; ++i) w.timed.push_back(Passive(gen, static_cast<OrderId>(i), levels));
    return w;
}

// Carnet de N ordres, puis annulation de tous dans un ordre aléatoire.
Workload MakeCancel(int orders, int levels) {
    std::mt19937_64 gen(2);
    Workload w;
    for (int i = 0; i < orders; ++i) w.setup.push_back(Passive(gen, static_cast<OrderId>(i), levels));
    std::vector<OrderId> ids(static_cast<std::size_t>(orders));
    for (int i = 0; i < orders; ++i) ids[static_cast<std::size_t>(i)] = static_cast<OrderId>(i);
    std::shuffle(ids.begin(), ids.end(), gen);
    for (const OrderId id: ids) w.timed.push_back(Op{id, 0, 0, OpKind::Cancel, Side::Buy, OrderType::GoodTillCancel});
    return w;
}

// Régime permanent autour de N ordres : 40 % d'ajouts passifs, 40 % d'annulations, 10 % de
// modifications, 10 % d'ordres agressifs (IOC qui traversent quelques ordres).
Workload MakeMixed(int orders, int levels) {
    std::mt19937_64 gen(3);
    Workload w;
    struct Live {
        OrderId id;
        Side side;
    };
    std::vector<Live> live;
    OrderId next = 0;
    for (int i = 0; i < orders; ++i) {
        const Op op = Passive(gen, next++, levels);
        live.push_back({op.id, op.side});
        w.setup.push_back(op);
    }
    const int operations = std::max(orders, 200000);
    for (int i = 0; i < operations; ++i) {
        const std::uint64_t r = gen() % 100;
        if (r < 40 || live.empty()) {
            const Op op = Passive(gen, next++, levels);
            live.push_back({op.id, op.side});
            w.timed.push_back(op);
        } else if (r < 80) {
            const std::size_t k = gen() % live.size();
            w.timed.push_back(Op{live[k].id, 0, 0, OpKind::Cancel, Side::Buy, OrderType::GoodTillCancel});
            live[k] = live.back();
            live.pop_back();
        } else if (r < 90) {
            const Live &l = live[gen() % live.size()];
            const int d = Depth(gen, levels);
            const Price price = l.side == Side::Buy ? kMid - 1 - d : kMid + 1 + d;
            w.timed.push_back(Op{l.id, price, static_cast<Quantity>(1 + gen() % 100), OpKind::Modify, l.side,
                                 OrderType::GoodTillCancel});
        } else {
            const Side side = (gen() & 1) ? Side::Buy : Side::Sell;
            const Price price = side == Side::Buy ? kMid + 2 : kMid - 2;
            w.timed.push_back(Op{next++, price, static_cast<Quantity>(1 + gen() % 150), OpKind::Add, side,
                                 OrderType::ImmediateOrCancel});
        }
    }
    return w;
}

// N ventes au repos, puis des achats au marché qui consomment chacun 64 ordres.
Workload MakeSweep(int orders, int levels) {
    std::mt19937_64 gen(4);
    Workload w;
    for (int i = 0; i < orders; ++i) {
        const Price price = kMid + 1 + static_cast<Price>(gen() % static_cast<std::uint64_t>(levels));
        w.setup.push_back(Op{static_cast<OrderId>(i), price, 10, OpKind::Add, Side::Sell, OrderType::GoodTillCancel});
    }
    for (int i = 0; i < orders / 64; ++i) {
        w.timed.push_back(Op{static_cast<OrderId>(orders + i), 0, 640, OpKind::Add, Side::Buy, OrderType::Market});
    }
    return w;
}

// ---------------------------------------------------------------------------------------------
// Mesure
// ---------------------------------------------------------------------------------------------

struct Result {
    double nsPerOp;
    double tscPerOp;
    std::uint64_t checksum;
};

// Remplace les identifiants séquentiels par des identifiants pseudo-aléatoires (bijection sur
// 64 bits) : pire cas pour la localité de la table d'identifiants.
void ScrambleIds(Workload &w) {
    auto scramble = [](OrderId id) {
        id = (id + 0x9E3779B97F4A7C15ull) * 0xBF58476D1CE4E5B9ull;
        id ^= id >> 29;
        id *= 0x94D049BB133111EBull;
        return id ^ (id >> 32);
    };
    for (Op &op: w.setup) op.id = scramble(op.id);
    for (Op &op: w.timed) op.id = scramble(op.id);
}

template<class Engine>
Result RunOnce(const Workload &w) {
    auto engine = std::make_unique<Engine>();
    engine->Reserve(w.setup.size() + w.timed.size());
    for (const Op &op: w.setup) engine->Apply(op);

    unsigned aux;
    const auto t0 = std::chrono::steady_clock::now();
    const std::uint64_t c0 = __rdtscp(&aux);
    for (const Op &op: w.timed) engine->Apply(op);
    const std::uint64_t c1 = __rdtscp(&aux);
    const auto t1 = std::chrono::steady_clock::now();

    const double n = static_cast<double>(w.timed.size());
    return Result{
        std::chrono::duration<double, std::nano>(t1 - t0).count() / n,
        static_cast<double>(c1 - c0) / n,
        engine->traded * 1000003u + engine->Size()
    };
}

template<class Engine>
void Run(const char *engine, const char *scenario, int orders, int levels, const Workload &w, int reps) {
    std::vector<double> ns, tsc;
    std::uint64_t checksum = 0;
    for (int r = 0; r < reps; ++r) {
        const Result result = RunOnce<Engine>(w);
        ns.push_back(result.nsPerOp);
        tsc.push_back(result.tscPerOp);
        checksum = result.checksum;
    }
    std::sort(ns.begin(), ns.end());
    std::sort(tsc.begin(), tsc.end());
    std::printf("%-12s %-10s %8d %6d %9.1f %9.1f %9.1f  %016llx\n", scenario, engine, orders, levels,
                ns[ns.size() / 2], ns.front(), tsc[tsc.size() / 2], static_cast<unsigned long long>(checksum));
    std::fflush(stdout);
}

using Fib50 = ob::IdTable<ob::FibonacciHash, 1, 2>;
using Fib75 = ob::IdTable<ob::FibonacciHash, 3, 4>;
using Ident = ob::IdTable<ob::IdentityHash, 1, 2>;
using Direct = ob::DirectIdTable<>;
#if defined(__SSE4_2__)
using Crc50 = ob::IdTable<ob::Crc32Hash, 1, 2>;
#endif

bool Wanted(const std::string &filter, const char *name) { return filter.empty() || filter == name; }
} // namespace

int main(int argc, char **argv) {
    std::string engineFilter, scenarioFilter;
    int onlyOrders = 0, onlyLevels = 0, reps = 7;
    bool randomIds = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg.rfind("--engine=", 0) == 0) engineFilter = arg.substr(9);
        else if (arg.rfind("--scenario=", 0) == 0) scenarioFilter = arg.substr(11);
        else if (arg.rfind("--orders=", 0) == 0) onlyOrders = std::atoi(arg.c_str() + 9);
        else if (arg.rfind("--levels=", 0) == 0) onlyLevels = std::atoi(arg.c_str() + 9);
        else if (arg.rfind("--reps=", 0) == 0) reps = std::atoi(arg.c_str() + 7);
        else if (arg == "--random-ids") randomIds = true;
        else {
            std::fprintf(stderr, "argument inconnu : %s\n", arg.c_str());
            return 2;
        }
    }

    struct Shape {
        int orders;
        int levels;
    };
    std::vector<Shape> shapes = {{1000, 20}, {100000, 200}, {100000, 20000}, {1000000, 2000}};
    if (onlyOrders) shapes = {{onlyOrders, onlyLevels ? onlyLevels : 200}};

    struct Scenario {
        const char *name;
        Workload (*make)(int, int);
    };
    const Scenario scenarios[] = {
        {"ajout", MakeAdd}, {"annulation", MakeCancel}, {"mixte", MakeMixed}, {"balayage", MakeSweep}
    };

    std::printf("%-12s %-10s %8s %6s %9s %9s %9s  %s\n", "scenario", "moteur", "ordres", "niv.",
                "ns/op", "ns min", "tsc/op", "controle");

    for (const Shape &shape: shapes) {
        for (const Scenario &scenario: scenarios) {
            if (!Wanted(scenarioFilter, scenario.name)) continue;
            Workload w = scenario.make(shape.orders, shape.levels);
            if (randomIds) ScrambleIds(w);
            const int n = shape.orders, l = shape.levels;

            // L'original est en O(taille du carnet) par ajout : on le limite aux petits carnets.
            if (Wanted(engineFilter, "origine") && n <= 10000)
                Run<RefEngine>("origine", scenario.name, n, l, w, std::min(reps, 3));
            if (Wanted(engineFilter, "compat")) Run<CompatEngine>("compat", scenario.name, n, l, w, reps);
            if (Wanted(engineFilter, "trie8"))
                Run<CoreEngine<ob::SortedLevels<8>, Fib50>>("trie8", scenario.name, n, l, w, reps);
            if (Wanted(engineFilter, "trie-dicho"))
                Run<CoreEngine<ob::SortedLevels<0>, Fib50>>("trie-dicho", scenario.name, n, l, w, reps);
            if (Wanted(engineFilter, "trie-lin"))
                Run<CoreEngine<ob::SortedLevels<1 << 30>, Fib50>>("trie-lin", scenario.name, n, l, w, reps);
            if (Wanted(engineFilter, "dense"))
                Run<CoreEngine<ob::DenseLevels, Fib50>>("dense", scenario.name, n, l, w, reps);
            if (Wanted(engineFilter, "map"))
                Run<CoreEngine<ob::MapLevels, Fib50>>("map", scenario.name, n, l, w, reps);
            if (Wanted(engineFilter, "dense-lf75"))
                Run<CoreEngine<ob::DenseLevels, Fib75>>("dense-lf75", scenario.name, n, l, w, reps);
            if (Wanted(engineFilter, "dense-direct"))
                Run<CoreEngine<ob::DenseLevels, Direct>>("dense-direct", scenario.name, n, l, w, reps);
            if (Wanted(engineFilter, "dense-ident"))
                Run<CoreEngine<ob::DenseLevels, Ident>>("dense-ident", scenario.name, n, l, w, reps);
#if defined(__SSE4_2__)
            if (Wanted(engineFilter, "dense-crc"))
                Run<CoreEngine<ob::DenseLevels, Crc50>>("dense-crc", scenario.name, n, l, w, reps);
#endif
        }
    }
    return 0;
}
