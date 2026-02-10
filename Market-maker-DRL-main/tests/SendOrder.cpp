#include "../src/network/OrderExecutor.hpp"  // Votre classe OrderExecutor
#include <iostream>
#include <string>

int main() {
    try {
        net::io_context ioc;
        
        ssl::context ssl_ctx{ssl::context::tlsv12_client};
        ssl_ctx.set_default_verify_paths();
        ssl_ctx.set_verify_mode(ssl::verify_peer);
        
        std::string api_key = "YOUR_API_KEY";
        std::string secret_key = "YOUR_API_SECRET";
        
        // Création de l'exécuteur d'ordres
        OrderExecutor executor(ioc, ssl_ctx, secret_key);
        
        // Connexion à Binance
        std::string host = "testnet.binance.vision";  
        
        if (!executor.connect(host)) {
            std::cerr << "Échec de la connexion à " << host << std::endl;
            return 1;
        }
        
        std::cout << "Connecté à " << host << std::endl;
        // Ordre LIMIT d'achat de 0.001 BTC à 50000 USDT
        std::string symbol = "BTCUSDT";
        std::string side = "BUY";          
        std::string type = "LIMIT";        
        std::string timeInForce = "GTC";   // Good Till Cancel
        std::string quantity = "0.001";    // Quantité en BTC
        std::string price = "50000";       // Prix en USDT
        
        std::string payload = 
            "symbol=" + symbol +
            "&side=" + side +
            "&type=" + type +
            "&timeInForce=" + timeInForce +
            "&quantity=" + quantity +
            "&price=" + price +
            "&recvWindow=5000";  
        
        // Envoi l'ordre
        std::cout << "\n=== Envoi de l'ordre ===" << std::endl;
        std::cout << "Symbol: " << symbol << std::endl;
        std::cout << "Side: " << side << std::endl;
        std::cout << "Type: " << type << std::endl;
        std::cout << "Quantity: " << quantity << std::endl;
        std::cout << "Price: " << price << std::endl;
        std::cout << "========================\n" << std::endl;
        
        bool success = executor.execute_action(
            host,
            "/api/v3/order",  
            api_key,
            payload
        );
        
        if (success) {
            std::cout << "\nOrdre placé avec succès!" << std::endl;
        } else {
            std::cerr << "\nÉchec de placement de l'ordre" << std::endl;
            return 1;
        }
        
        executor.disconnect();
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Erreur fatale: " << e.what() << std::endl;
        return 1;
    }
}