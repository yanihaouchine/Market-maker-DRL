#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/error.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <openssl/hmac.h>
#include <boost/beast/core.hpp>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <iostream>  

namespace http = boost::beast::http;
namespace beast = boost::beast;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;

class OrderExecutor {
private:
    tcp::resolver resolver_;
    ssl::stream<beast::tcp_stream> stream_;
    std::string secret_key_;
    bool connected_ = false;

public :
    // Génération du timestamp en millisecondes
    static int64_t get_timestamp() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
    }

    // Signature HMAC-SHA256
    std::string hmac_sha256(const std::string& data, const std::string& key) {
        unsigned char* digest = HMAC(
            EVP_sha256(),
            key.c_str(), key.length(),
            reinterpret_cast<const unsigned char*>(data.c_str()), data.length(),
            nullptr, nullptr
        );

        std::ostringstream oss;
        for (int i = 0; i < 32; ++i) {
            oss << std::hex << std::setw(2) << std::setfill('0') 
                << static_cast<int>(digest[i]);
        }
        return oss.str();
    }

public:
    OrderExecutor(net::io_context& ioc, ssl::context& ctx, const std::string& secret_key)
        : resolver_(ioc), 
          stream_(ioc, ctx),
          secret_key_(secret_key) {}

    // Connexion persistante
    bool connect(const std::string& host) {
        if (connected_) return true;

        try {
            // SNI CRITIQUE pour Binance
            if (!SSL_set_tlsext_host_name(stream_.native_handle(), host.c_str())) {
                throw beast::system_error(
                    beast::error_code(static_cast<int>(::ERR_get_error()),
                                     net::error::get_ssl_category()));
            }

            auto const results = resolver_.resolve(host, "443");
            
            beast::get_lowest_layer(stream_).expires_after(std::chrono::seconds(10));
            beast::get_lowest_layer(stream_).connect(results);
            
            stream_.handshake(ssl::stream_base::client);
            
            connected_ = true;
            return true;

        } catch (const std::exception& e) {
            std::cerr << "Connection failed: " << e.what() << std::endl;
            connected_ = false;
            return false;
        }
    }

    bool execute_action(const std::string& host, 
                       const std::string& target, 
                       const std::string& api_key, 
                       const std::string& payload) {
        try {
            // Connexion si nécessaire
            if (!connected_ && !connect(host)) {
                return false;
            }

            // Signature du payload
            std::string query = payload + "&timestamp=" + std::to_string(get_timestamp());
            std::string signature = hmac_sha256(query, secret_key_);
            std::string signed_payload = query + "&signature=" + signature;

            // Construction de la requête HTTP POST
            http::request<http::string_body> req{http::verb::post, target, 11};
            req.set(http::field::host, host);
            req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
            req.set("X-MBX-APIKEY", api_key);
            req.set(http::field::content_type, "application/x-www-form-urlencoded");
            req.body() = signed_payload;
            req.prepare_payload();

            // Timeout pour la requête
            beast::get_lowest_layer(stream_).expires_after(std::chrono::seconds(5));

            // Envoi
            http::write(stream_, req);

            // Réception
            beast::flat_buffer buffer;
            http::response<http::dynamic_body> res;
            http::read(stream_, buffer, res);

            // Vérification du statut
            if (res.result() != http::status::ok && 
                res.result() != http::status::created) {
                std::cerr << "Order rejected - Status: " << res.result_int() << std::endl;
                std::cerr << "Response: " << beast::buffers_to_string(res.body().data()) 
                         << std::endl;
                return false;
            }

            std::cout << "Order executed successfully: " 
                     << beast::buffers_to_string(res.body().data()) << std::endl;
            
            return true;

        } catch (const std::exception& e) {
            std::cerr << "Execution error: " << e.what() << std::endl;
            connected_ = false; // Forcer reconnexion au prochain appel
            return false;
        }
    }

    void disconnect() {
        if (connected_) {
            beast::error_code ec;
            stream_.shutdown(ec);
            connected_ = false;
        }
    }

    ~OrderExecutor() {
        disconnect();
    }
};