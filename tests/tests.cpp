#include <iostream>
#include <cstdlib>
#include <iomanip>
#include "OrderBook/OrderBook.h"
#include "OrderBook/Order.h"
#include "Core/Types.h"
#include "Core/OrderType.h"

static int testsRun = 0;

#define TEST(name) void name()
#define RUN_TEST(name) do { \
    std::cout << "Running " << #name << "... "; \
    name(); \
    ++testsRun; \
    std::cout << "PASSED\n"; \
} while(0)

// Unlike assert(), stays active in Release builds (-DNDEBUG).
#define CHECK(cond) do { \
    if (!(cond)) { \
        std::cerr << "\n" << __FILE__ << ":" << __LINE__ << ": check failed: " << #cond << "\n"; \
        std::abort(); \
    } \
} while(0)

#define ASSERT_EQ(a, b) CHECK((a) == (b))
#define ASSERT_TRUE(x) CHECK(x)
#define ASSERT_FALSE(x) CHECK(!(x))

TEST(TestBasicAddOrder) {
    Orderbook orderbook;
    auto order = std::make_shared<Order>(OrderType::GoodTillCancel, 1, Side::Buy, 100, 10);
    orderbook.AddOrder(order);
    ASSERT_EQ(orderbook.Size(), 1);
}

TEST(TestCancelOrder) {
    Orderbook orderbook;
    OrderId orderId = 1;
    orderbook.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, orderId, Side::Buy, 100, 10));
    ASSERT_EQ(orderbook.Size(), 1);
    orderbook.CancelOrder(orderId);
    ASSERT_EQ(orderbook.Size(), 0);
}

TEST(TestDuplicateOrderRejection) {
    Orderbook orderbook;
    OrderId orderId = 1;
    orderbook.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, orderId, Side::Buy, 100, 10));
    auto trades = orderbook.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, orderId, Side::Buy, 100, 10));
    ASSERT_EQ(orderbook.Size(), 1);
    ASSERT_TRUE(trades.empty());
}

TEST(TestSimpleMatch) {
    Orderbook orderbook;
    orderbook.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 1, Side::Buy, 100, 10));
    auto trades = orderbook.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 2, Side::Sell, 100, 10));

    ASSERT_EQ(trades.size(), 1);
    ASSERT_EQ(orderbook.Size(), 0);
    ASSERT_EQ(trades[0].GetBidTrade().quantity_, 10);
    ASSERT_EQ(trades[0].GetAskTrade().quantity_, 10);
}

TEST(TestPartialMatch) {
    Orderbook orderbook;
    orderbook.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 1, Side::Buy, 100, 15));
    auto trades = orderbook.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 2, Side::Sell, 100, 10));

    ASSERT_EQ(trades.size(), 1);
    ASSERT_EQ(orderbook.Size(), 1);
    ASSERT_EQ(trades[0].GetBidTrade().quantity_, 10);
}

TEST(TestMultipleMatchesAtSamePrice) {
    Orderbook orderbook;
    orderbook.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 1, Side::Buy, 100, 5));
    orderbook.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 2, Side::Buy, 100, 5));
    orderbook.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 3, Side::Buy, 100, 5));

    auto trades = orderbook.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 4, Side::Sell, 100, 12));

    ASSERT_EQ(trades.size(), 3);
    ASSERT_EQ(orderbook.Size(), 1);
}

TEST(TestPricePriority) {
    Orderbook orderbook;
    orderbook.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 1, Side::Buy, 100, 10));
    orderbook.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 2, Side::Buy, 105, 10));

    auto trades = orderbook.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 3, Side::Sell, 100, 10));

    ASSERT_EQ(trades.size(), 1);
    ASSERT_EQ(trades[0].GetBidTrade().orderId_, 2);
    ASSERT_EQ(trades[0].GetBidTrade().price_, 105); // checks if it sold to highest bidder
}

TEST(TestTimePriority_FIFO) {
    Orderbook orderbook;
    orderbook.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 1, Side::Buy, 100, 10));
    orderbook.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 2, Side::Buy, 100, 10));

    auto trades = orderbook.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 3, Side::Sell, 100, 10));

    ASSERT_EQ(trades.size(), 1);
    ASSERT_EQ(trades[0].GetBidTrade().orderId_, 1);
}

TEST(TestMarketOrderBuy) {
    Orderbook orderbook;
    orderbook.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 1, Side::Sell, 100, 10));

    auto marketOrder = std::make_shared<Order>(2, Side::Buy, 10);
    auto trades = orderbook.AddOrder(marketOrder);

    ASSERT_EQ(trades.size(), 1);
    ASSERT_EQ(orderbook.Size(), 0);
}

TEST(TestMarketOrderSell) {
    Orderbook orderbook;
    orderbook.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 1, Side::Buy, 100, 10));

    auto marketOrder = std::make_shared<Order>(2, Side::Sell, 10);
    auto trades = orderbook.AddOrder(marketOrder);

    ASSERT_EQ(trades.size(), 1);
    ASSERT_EQ(orderbook.Size(), 0);
}

TEST(TestMarketOrderEmptyBook) {
    Orderbook orderbook;
    auto marketOrder = std::make_shared<Order>(1, Side::Buy, 10);
    auto trades = orderbook.AddOrder(marketOrder);

    ASSERT_TRUE(trades.empty());
    ASSERT_EQ(orderbook.Size(), 0);
}

TEST(TestImmediateOrCancel_PartialFill) {
    Orderbook orderbook;
    orderbook.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 1, Side::Sell, 100, 5));

    auto fakOrder = std::make_shared<Order>(OrderType::ImmediateOrCancel, 2, Side::Buy, 100, 10);
    auto trades = orderbook.AddOrder(fakOrder);

    ASSERT_EQ(trades.size(), 1);
    ASSERT_EQ(trades[0].GetBidTrade().quantity_, 5);
    ASSERT_EQ(orderbook.Size(), 0);
}

TEST(TestImmediateOrCancel_NoMatch) {
    Orderbook orderbook;
    orderbook.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 1, Side::Sell, 105, 10));

    auto fakOrder = std::make_shared<Order>(OrderType::ImmediateOrCancel, 2, Side::Buy, 100, 10);
    auto trades = orderbook.AddOrder(fakOrder);

    ASSERT_TRUE(trades.empty());
    ASSERT_EQ(orderbook.Size(), 1);
}

TEST(TestFillOrKill_FullFill) {
    Orderbook orderbook;
    orderbook.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 1, Side::Sell, 100, 10));

    auto fokOrder = std::make_shared<Order>(OrderType::FillOrKill, 2, Side::Buy, 100, 10);
    auto trades = orderbook.AddOrder(fokOrder);

    ASSERT_EQ(trades.size(), 1);
    ASSERT_EQ(trades[0].GetBidTrade().quantity_, 10);
    ASSERT_EQ(orderbook.Size(), 0);
}

TEST(TestFillOrKill_PartialAvailable) {
    Orderbook orderbook;
    orderbook.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 1, Side::Sell, 100, 5));

    auto fokOrder = std::make_shared<Order>(OrderType::FillOrKill, 2, Side::Buy, 100, 10);
    auto trades = orderbook.AddOrder(fokOrder);

    ASSERT_TRUE(trades.empty());
    ASSERT_EQ(orderbook.Size(), 1);
}

TEST(TestFillOrKill_MultipleOrders) {
    Orderbook orderbook;
    orderbook.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 1, Side::Sell, 100, 5));
    orderbook.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 2, Side::Sell, 100, 5));

    auto fokOrder = std::make_shared<Order>(OrderType::FillOrKill, 3, Side::Buy, 100, 10);
    auto trades = orderbook.AddOrder(fokOrder);

    ASSERT_EQ(trades.size(), 2);
    ASSERT_EQ(orderbook.Size(), 0);
}

TEST(TestOrderModify) {
    Orderbook orderbook;
    OrderId orderId = 1;
    orderbook.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, orderId, Side::Buy, 100, 10));

    OrderModify modify(orderId, Side::Buy, 105, 15);
    orderbook.MatchOrder(modify);

    ASSERT_EQ(orderbook.Size(), 1);
    auto infos = orderbook.GetOrderInfos();
    ASSERT_EQ(infos.GetBids()[0].price_, 105);
    ASSERT_EQ(infos.GetBids()[0].quantity_, 15);
}

TEST(TestOrderbookLevelInfos) {
    Orderbook orderbook;
    orderbook.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 1, Side::Buy, 100, 10));
    orderbook.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 2, Side::Buy, 100, 5));
    orderbook.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 3, Side::Sell, 105, 20));

    auto infos = orderbook.GetOrderInfos();
    ASSERT_EQ(infos.GetBids().size(), 1);
    ASSERT_EQ(infos.GetBids()[0].quantity_, 15);
    ASSERT_EQ(infos.GetAsks().size(), 1);
    ASSERT_EQ(infos.GetAsks()[0].quantity_, 20);
}

TEST(TestExchangeRulesBasic) {
    Orderbook orderbook;
    ExchangeRules rules;
    rules.tickSize = 5; // Prices in multiples of 5 cents
    rules.lotSize = 10; // Quantities in multiples of 10
    rules.minQuantity = 10; // Minimum 10 shares
    orderbook.SetExchangeRules(rules);

    // Valid order
    auto validOrder = std::make_shared<Order>(OrderType::GoodTillCancel, 1, Side::Buy, 100, 20);
    orderbook.AddOrder(validOrder);
    ASSERT_EQ(orderbook.Size(), 1);

    // Invalid tick size
    auto invalidTick = std::make_shared<Order>(OrderType::GoodTillCancel, 2, Side::Buy, 103, 20);
    orderbook.AddOrder(invalidTick);
    ASSERT_EQ(orderbook.Size(), 1); // Rejected

    // Invalid lot size
    auto invalidLot = std::make_shared<Order>(OrderType::GoodTillCancel, 3, Side::Buy, 100, 15);
    orderbook.AddOrder(invalidLot);
    ASSERT_EQ(orderbook.Size(), 1); // Rejected

    // Below minimum quantity
    auto belowMin = std::make_shared<Order>(OrderType::GoodTillCancel, 4, Side::Buy, 100, 5);
    orderbook.AddOrder(belowMin);
    ASSERT_EQ(orderbook.Size(), 1); // Rejected
}

TEST(TestMinNotionalValidation) {
    Orderbook orderbook;
    ExchangeRules rules;
    rules.minNotional = 1000; // Minimum order value is 1000 cents ($10)
    orderbook.SetExchangeRules(rules);

    // Valid: 150 * 10 = 1500 >= 1000
    auto validOrder = std::make_shared<Order>(OrderType::GoodTillCancel, 1, Side::Buy, 150, 10);
    orderbook.AddOrder(validOrder);
    ASSERT_EQ(orderbook.Size(), 1);

    // Invalid: 50 * 10 = 500 < 1000
    auto invalidOrder = std::make_shared<Order>(OrderType::GoodTillCancel, 2, Side::Buy, 50, 10);
    orderbook.AddOrder(invalidOrder);
    ASSERT_EQ(orderbook.Size(), 1); // Rejected
}

TEST(TestMarketOrderValidation) {
    Orderbook orderbook;
    ExchangeRules rules;
    rules.lotSize = 10;
    orderbook.SetExchangeRules(rules);
    // Add liquidity
    orderbook.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 1, Side::Sell, 100, 50));

    // Valid market order (quantity is multiple of 10)
    auto validMarket = std::make_shared<Order>(2, Side::Buy, 20);
    auto trades = orderbook.AddOrder(validMarket);
    ASSERT_EQ(trades.size(), 1);

    // Invalid market order (quantity not multiple of 10)
    auto invalidMarket = std::make_shared<Order>(3, Side::Buy, 15);
    auto noTrades = orderbook.AddOrder(invalidMarket);
    ASSERT_TRUE(noTrades.empty()); // Rejected
}

int main() {
    std::cout << std::string(70, '=') << "\n";
    std::cout << std::setw(45) << "ORDERBOOK FUNCTIONALITY TESTS\n";
    std::cout << std::string(70, '=') << "\n\n";

    RUN_TEST(TestBasicAddOrder);
    RUN_TEST(TestCancelOrder);
    RUN_TEST(TestDuplicateOrderRejection);
    RUN_TEST(TestSimpleMatch);
    RUN_TEST(TestPartialMatch);
    RUN_TEST(TestMultipleMatchesAtSamePrice);
    RUN_TEST(TestPricePriority);
    RUN_TEST(TestTimePriority_FIFO);
    RUN_TEST(TestMarketOrderBuy);
    RUN_TEST(TestMarketOrderSell);
    RUN_TEST(TestMarketOrderEmptyBook);
    RUN_TEST(TestImmediateOrCancel_PartialFill);
    RUN_TEST(TestImmediateOrCancel_NoMatch);
    RUN_TEST(TestFillOrKill_FullFill);
    RUN_TEST(TestFillOrKill_PartialAvailable);
    RUN_TEST(TestFillOrKill_MultipleOrders);
    RUN_TEST(TestOrderModify);
    RUN_TEST(TestOrderbookLevelInfos);
    RUN_TEST(TestExchangeRulesBasic);
    RUN_TEST(TestMinNotionalValidation);
    RUN_TEST(TestMarketOrderValidation);

    std::cout << "\nAll " << testsRun << " functionality tests passed!\n";

    return 0;
}
