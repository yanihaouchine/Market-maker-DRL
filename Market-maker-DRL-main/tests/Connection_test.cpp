#include <iostream>
#include <string_view>
#include "../src/network/ConnectionBinance.hpp" 

int main() {
    try {
        // Initialisation des composants Boost
        boost::asio::io_context ioc;
        boost::asio::ssl::context ctx{boost::asio::ssl::context::tlsv12_client};

        // charger les certificats système pour SSL
        ctx.set_default_verify_paths();

        // Création de la connexion
        hft::network::ConnectionBinance conn(ioc, ctx);

        // Définition du MessageHandler (Ce qu'on fait quand on reçoit des octets)
        conn.set_message_handler([](std::span<const uint8_t> data) {
            // On transforme les octets en string_view pour l'affichage
            std::string_view msg(reinterpret_cast<const char*>(data.data()), data.size());
            std::cout << msg << std::endl;
        });

        // Tentative de connexion (URL de Binance et port 9443)
        std::cout << "Connexion à Binance en cours..." << std::endl;
        if (conn.connect("stream.binance.com", 9443)) {
            std::cout << "Connecté ! Écoute du flux BTC/USDT..." << std::endl;

            // Boucle de lecture infinie
            while (true) {
                conn.poll();
            }
        } else {
            std::cerr << "La connexion a échoué." << std::endl;
        }

    } catch (const std::exception& e) {
        std::cerr << "Erreur critique : " << e.what() << std::endl;
    }

    return 0;
}