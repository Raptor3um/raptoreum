//
// Created by tri on 8/8/24.
//

#include <qt/uploaddownload.h>

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/connect.hpp>
#include <QString>
#include <QFileDialog>
#include <filesystem>

namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace http = beast::http;           // from <boost/beast/http.hpp>
namespace net = boost::asio;            // from <boost/asio.hpp>
namespace ssl = net::ssl;               // from <boost/asio/ssl.hpp>
using tcp = net::ip::tcp;               // from <boost/asio/ip/tcp.hpp>

ssl::context setup_ssl_context() {
    ssl::context ctx{ssl::context::tlsv12_client};
    ctx.set_options(
            ssl::context::default_workarounds |
            ssl::context::no_sslv3 |
            ssl::context::no_tlsv1 |
            ssl::context::no_tlsv1_1 |
            ssl::context::single_dh_use
    );
    return ctx;
}

beast::ssl_stream<beast::tcp_stream> create_and_connect_ssl_stream(net::io_context& ioc, const std::string& host, const std::string& port, ssl::context& ctx) {
    tcp::resolver resolver{ioc};
    beast::ssl_stream<beast::tcp_stream> stream{ioc, ctx};

    if (!SSL_set_tlsext_host_name(stream.native_handle(), host.c_str())) {
        beast::error_code ec{static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()};
        throw beast::system_error{ec};
    }

    auto const results = resolver.resolve(host, port);
    beast::get_lowest_layer(stream).connect(results);
    stream.handshake(ssl::stream_base::client);

    return stream;
}

template <typename Body>
std::string write_response(beast::ssl_stream<beast::tcp_stream>& stream, http::request<Body>& req) {
    try {
        // Send the HTTP request to the remote host
        http::write(stream, req);

        // Declare a container to hold the response
        beast::flat_buffer buffer;
        http::response<http::dynamic_body> res;
        http::read(stream, buffer, res);

        // Output the response
        std::ostringstream response_stream;
        response_stream << beast::buffers_to_string(res.body().data());
        return response_stream.str();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        throw;
    }
}

std::string generate_boundary() {
    static const char alphanum[] =
            "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    const size_t len = 32;
    std::string boundary;
    boundary.reserve(len);

    for (size_t i = 0; i < len; ++i) {
        boundary += alphanum[rand() % (sizeof(alphanum) - 1)];
    }
    return boundary;
}

void graceful_disconnect(beast::ssl_stream<beast::tcp_stream>& stream) {
    beast::error_code ec;
    stream.shutdown(ec);
    if (ec && ec != boost::asio::error::eof && ec != boost::asio::ssl::error::stream_truncated) {
        throw boost::system::system_error{ec};
    }
}

void download(const std::string cid, std::string& response_data) {
    std::string getTarget = GET_URI + cid;
    try {
        net::io_context ioc;
        ssl::context ctx = setup_ssl_context();
        beast::ssl_stream<beast::tcp_stream> stream = create_and_connect_ssl_stream(ioc, IPFS_SERVICE_HOST, "443", ctx);

        http::request<http::empty_body> req{http::verb::get, getTarget, 11};
        req.set(http::field::host, IPFS_SERVICE_HOST);
        req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

        response_data = write_response(stream, req);
        graceful_disconnect(stream);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        LogPrintf("Error: %s \n", e.what());
    }
}

void upload(const std::string& file_path, std::string& response_data) {
    try {
        net::io_context ioc;
        ssl::context ctx = setup_ssl_context();
        beast::ssl_stream <beast::tcp_stream> stream = create_and_connect_ssl_stream(ioc, IPFS_SERVICE_HOST, "443", ctx);

        // Generate a boundary for the multipart form data
        std::string boundary = generate_boundary();

        // Read the file content
        std::ifstream file(file_path, std::ios::binary);
        if (!file) {
            throw std::runtime_error("Failed to open file");
        }
        std::filesystem::path full_path = file_path;
        std::string file_content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

        // Create the multipart form data body
        std::ostringstream body;
        body << "--" << boundary << "\r\n";
        body << "Content-Disposition: form-data; name=\"file\"; filename=\"" << full_path.filename().string() << "\"\r\n";
        body << "Content-Type: application/octet-stream\r\n\r\n";
        body << file_content << "\r\n";
        body << "--" << boundary << "--\r\n";

        std::string body_str = body.str();

        http::request <http::string_body> req{http::verb::post, UPLOAD_URI, 11};
        req.set(http::field::host, IPFS_SERVICE_HOST);
        req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
        req.set(http::field::content_type, "multipart/form-data; boundary=" + boundary);
        req.set(http::field::content_length, std::to_string(body_str.size()));
        req.body() = body_str;

        response_data = write_response(stream, req);

        graceful_disconnect(stream);
    } catch (const std::exception &e) {
        LogPrintf("Error: %s", e.what());
        response_data = e.what();
    }
}

void pickAndUploadFileForIpfs(QWidget *qWidget, std::string& cid) {
    QString filePath = QFileDialog::getOpenFileName(qWidget, "Open File", "", "All Files (*.*)");
    if (!filePath.isEmpty()) {
        std::string filePathStr = filePath.toStdString();
        upload(filePathStr, cid);
    }
}