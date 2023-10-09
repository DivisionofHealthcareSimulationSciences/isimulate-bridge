// Copyright (c) 2023 Rainer Leuschke
// University of Washington, CREST lab

#include "amm/BaseLogger.h"
#include "websocket_session.hpp"

websocket_session::websocket_session(net::io_context& ioc)
   : resolver_(net::make_strand(ioc))
   , ws_(net::make_strand(ioc))
{
}

websocket_session::~websocket_session()
{
}

void websocket_session::run(
   std::string host,
   std::string port,
   std::string target)
{
   // Save for later
   host_ = host;
   target_ = target;

   // Look up the domain name
   resolver_.async_resolve(
      host,
      port,
      beast::bind_front_handler(
            &websocket_session::on_resolve,
            shared_from_this()));
}

void websocket_session::fail(error_code ec, char const* what)
{
   // Don't report these
   if( ec == net::error::operation_aborted ||
      ec == websocket::error::closed)
      return;

   LOG_ERROR << what << ": " << ec.message();
}

void websocket_session::on_resolve(
   error_code ec,
   tcp::resolver::results_type results)
{
   if(ec) return fail(ec, "resolve");
   for(tcp::endpoint const& endpoint : results) {
      LOG_INFO << "websocket resolved endpoint: " << endpoint;
   }

   // Set the timeout for the operation
   beast::get_lowest_layer(ws_).expires_after(std::chrono::seconds(10));

   // Make the connection on the IP address we get from a lookup
   beast::get_lowest_layer(ws_).async_connect(
      results,
      beast::bind_front_handler(
         &websocket_session::on_connect,
         shared_from_this()));
}

void websocket_session::on_connect(
   error_code ec,
   tcp::resolver::results_type::endpoint_type ep)
{
   if(ec) return fail(ec, "connect");
   LOG_INFO << "websocket connected ";

   // Turn off the timeout on the tcp_stream, because
   // the websocket stream has its own timeout system.
   beast::get_lowest_layer(ws_).expires_never();

   // Set suggested timeout settings for the websocket
   ws_.set_option(
      websocket::stream_base::timeout::suggested(
         beast::role_type::client));

   // Set a decorator to change the User-Agent of the handshake
   ws_.set_option(websocket::stream_base::decorator(
      [](websocket::request_type& req)
      {
         req.set(http::field::user_agent,
               std::string(BOOST_BEAST_VERSION_STRING) +
                  " websocket-client-async");
      }));

   // Update the host_ string. This will provide the value of the
   // Host HTTP header during the WebSocket handshake.
   // See https://tools.ietf.org/html/rfc7230#section-5.4
   host_ += ':' + std::to_string(ep.port());

   // Perform the websocket handshake
   ws_.async_handshake(host_, target_,
      beast::bind_front_handler(
         &websocket_session::on_handshake,
         shared_from_this()));
}

void websocket_session::on_handshake(error_code ec)
{
   if(ec) return fail(ec, "handshake");
   LOG_INFO << "websocket handshake successful";

   // read a message when available
   ws_.async_read(
      buffer_,
      beast::bind_front_handler(
         &websocket_session::on_read,
         shared_from_this()));
}

void websocket_session::do_write(std::string message)
{
   LOG_INFO << "websocket writing message:" << message;
   // Send the message
   ws_.async_write(
      net::buffer(message),
      beast::bind_front_handler(
            &websocket_session::on_write,
            shared_from_this()));
}

void websocket_session::on_write(
   error_code ec,
   std::size_t bytes_transferred)
{
   boost::ignore_unused(bytes_transferred);

   if(ec) return fail(ec, "write");
   //LOG_INFO << "websocket message written: " << bytes_transferred;
}

void websocket_session::registerReadCallback(std::function<void(std::string)> cb)
{
   readCallback = std::bind(cb, std::placeholders::_1);
}

void websocket_session::on_read(
   error_code ec,
   std::size_t bytes_transferred)
{
   boost::ignore_unused(bytes_transferred);

   if(ec) return fail(ec, "read");

   //LOG_INFO << "websocket message: " << beast::make_printable(buffer_.data());
   if (readCallback) readCallback(beast::buffers_to_string(buffer_.data()));

   // Clear the buffer
   buffer_.consume(buffer_.size());

   // read another message when available
   ws_.async_read(
      buffer_,
      beast::bind_front_handler(
         &websocket_session::on_read,
         shared_from_this()));
}

void websocket_session::do_close()
{
   // Close the WebSocket connection
   LOG_INFO << "websocket closing";

   ws_.async_close(websocket::close_code::normal,
      beast::bind_front_handler(
         &websocket_session::on_close,
         shared_from_this()));
}

void websocket_session::on_close(error_code ec)
{
   if(ec) return fail(ec, "close");

   // If we get here then the connection is closed gracefully
   LOG_INFO << "websocket closed gracefully";
}
