#pragma once

#include <cassert>
#include <stdexcept>

#include "Core/Constants.h"
#include "Core/OrderType.h"
#include "Core/Types.h"

// Ordre vu par l'appelant (API de compatibilité). Le moteur en garde sa propre copie compacte
// (ob::OrderNode) : après AddOrder, cet objet reflète l'exécution immédiate de l'ordre, pas les
// exécutions ultérieures de son reliquat inscrit au carnet.
class Order {
public:
    Order(OrderType orderType, OrderId orderId, Side side, Price price, Quantity quantity)
        : orderType_{orderType}
          , orderId_{orderId}
          , side_{side}
          , price_{price}
          , initialQuantity_{quantity}
          , remainingQuantity_{quantity} {
        if (quantity == 0) {
            throw std::invalid_argument("Order quantity must be greater than zero");
        }
    }

    Order(OrderId orderId, Side side, Quantity quantity)
        : Order(OrderType::Market, orderId, side, Constants::InvalidPrice, quantity) {
    }

    OrderId GetOrderId() const { return orderId_; }
    Side GetSide() const { return side_; }
    Price GetPrice() const { return price_; }
    OrderType GetOrderType() const { return orderType_; }
    Quantity GetInitialQuantity() const { return initialQuantity_; }
    Quantity GetRemainingQuantity() const { return remainingQuantity_; }
    Quantity GetFilledQuantity() const { return initialQuantity_ - remainingQuantity_; }
    bool IsFilled() const { return remainingQuantity_ == 0; }

    // Préconditions vérifiées en Debug uniquement.
    void Fill(Quantity quantity) {
        assert(quantity <= remainingQuantity_ && "Order cannot be filled for more than its remaining quantity");
        remainingQuantity_ -= quantity;
    }

    void ToGoodTillCancel(Price price) {
        assert(orderType_ == OrderType::Market && "Cannot convert non-market order to GoodTillCancel");
        price_ = price;
        orderType_ = OrderType::GoodTillCancel;
    }

private:
    OrderType orderType_;
    OrderId orderId_;
    Side side_;
    Price price_;
    Quantity initialQuantity_;
    Quantity remainingQuantity_;
};
