#include "WSClient.h"
#include "../proto/WSClient.pb.h"

#include <iostream>
#include <chrono>
//#include <format>
#include "uuid.h"
#include "glob.hpp"

#ifdef _WIN32
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#endif

#define TAG "WSClient"

using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;
using websocketpp::lib::bind;

namespace WSClient
{

#ifndef STATIC_API
	extern "C" PUBLIC_API BioSys::RNA* CreateInstance(BioSys::IBiomolecule* owner)
	{
		return new WSClient(owner);
	}
#endif
    /// Verify that one of the subject alternative names matches the given hostname
    bool WSClient::verify_subject_alternative_name(const char* hostname, X509* cert) {
        STACK_OF(GENERAL_NAME)* san_names = NULL;

        san_names = (STACK_OF(GENERAL_NAME)*) X509_get_ext_d2i(cert, NID_subject_alt_name, NULL, NULL);
        if (san_names == NULL) {
            return false;
        }

        int san_names_count = sk_GENERAL_NAME_num(san_names);

        bool result = false;

        for (int i = 0; i < san_names_count; i++) {
            const GENERAL_NAME* current_name = sk_GENERAL_NAME_value(san_names, i);

            if (current_name->type != GEN_DNS) {
                continue;
            }

            char const* dns_name = (char const*)ASN1_STRING_get0_data(current_name->d.dNSName);

            // Make sure there isn't an embedded NUL character in the DNS name
            if (ASN1_STRING_length(current_name->d.dNSName) != strlen(dns_name)) {
                break;
            }
            // Compare expected hostname with the CN
            result = (strcasecmp(hostname, dns_name) == 0);
        }
        sk_GENERAL_NAME_pop_free(san_names, GENERAL_NAME_free);

        return result;
    }

    /// Verify that the certificate common name matches the given hostname
    bool WSClient::verify_common_name(char const* hostname, X509* cert) {
        // Find the position of the CN field in the Subject field of the certificate
        int common_name_loc = X509_NAME_get_index_by_NID(X509_get_subject_name(cert), NID_commonName, -1);
        if (common_name_loc < 0) {
            return false;
        }

        // Extract the CN field
        X509_NAME_ENTRY* common_name_entry = X509_NAME_get_entry(X509_get_subject_name(cert), common_name_loc);
        if (common_name_entry == NULL) {
            return false;
        }

        // Convert the CN field to a C string
        ASN1_STRING* common_name_asn1 = X509_NAME_ENTRY_get_data(common_name_entry);
        if (common_name_asn1 == NULL) {
            return false;
        }

        char const* common_name_str = (char const*)ASN1_STRING_get0_data(common_name_asn1);

        // Make sure there isn't an embedded NUL character in the CN
        if (ASN1_STRING_length(common_name_asn1) != strlen(common_name_str)) {
            return false;
        }

        // Compare expected hostname with the CN
        return (strcasecmp(hostname, common_name_str) == 0);
    }

    bool WSClient::verify_certificate(const char* hostname, bool preverified, asio::ssl::verify_context& ctx) {
        // The verify callback can be used to check whether the certificate that is
        // being presented is valid for the peer. For example, RFC 2818 describes
        // the steps involved in doing this for HTTPS. Consult the OpenSSL
        // documentation for more details. Note that the callback is called once
        // for each certificate in the certificate chain, starting from the root
        // certificate authority.

        // Retrieve the depth of the current cert in the chain. 0 indicates the
        // actual server cert, upon which we will perform extra validation
        // (specifically, ensuring that the hostname matches. For other certs we
        // will use the 'preverified' flag from Asio, which incorporates a number of
        // non-implementation specific OpenSSL checking, such as the formatting of
        // certs and the trusted status based on the CA certs we imported earlier.
        int depth = X509_STORE_CTX_get_error_depth(ctx.native_handle());

        // if we are on the final cert and everything else checks out, ensure that
        // the hostname is present on the list of SANs or the common name (CN).
        if (depth == 0 && preverified) {
        //if (depth == 0) {
            X509* cert = X509_STORE_CTX_get_current_cert(ctx.native_handle());

            if (verify_subject_alternative_name(hostname, cert)) {
                return true;
            }
            else if (verify_common_name(hostname, cert)) {
                return true;
            }
            else {
                return false;
            }
        }
        return preverified;
    }

    void WSClient::on_connected(const String& id, const String& request, websocketpp::connection_hdl hdl) {
        //using namespace std::chrono;
        //time_point<utc_clock> _now = time_point_cast<std::chrono::milliseconds>(utc_clock::now());
        //std::string _formatted_time = std::format("{0:%F}T{0:%T}", _now);
        //_formatted_time = _formatted_time.substr(0, _formatted_time.size() - 4) + "Z";
        //const std::string _subscribe =
        //    R"({"op": "subscribe","args": [{"channel": "bbo-tbt","instId": "BTC-USDT"}]})";
        MutexLocker _connection_map_guard(WSClient::connection_map_mutex_);
        if (connection_map_.count(id) > 0)
        {
            connection_map_[id]->hdl_ = hdl;
            websocketpp::lib::error_code ec;
            if (connection_map_[id]->is_tls_)
                connection_map_[id]->tls_client_.send(hdl, request, websocketpp::frame::opcode::text, ec);
            else
                connection_map_[id]->client_.send(hdl, request, websocketpp::frame::opcode::text, ec);
            LOG_I(TAG, "WSClient.on_connected(%s) send request: %s", id.c_str(), request.c_str());
            if (ec) {
                LOG_W(TAG, "Send request: %s\nfailed because: %s", request.c_str(), ec.message().c_str());
            }

            if (connection_map_[id]->owner_ != nullptr)
            {
                Subscribe::Result _result;
                _result.set_id(id);
                _result.set_first_one(true);
                connection_map_[id]->owner_->SendEvent(Subscribe::Result::descriptor()->full_name(), _result.SerializeAsString());
            }
            connection_map_[id]->ref_count_++;
        }
        else
        {
            Subscribe::Error _error;
            _error.set_id(id);
            _error.set_err_code(-1);
            _error.set_err_message("No such instance in connection_map");
            //connection_map_[id]->owner_->SendEvent(Subscribe::Error::descriptor()->full_name(), _error.SerializeAsString());
            LOG_E(TAG, "WSClient.on_connected(%s) error code=%d, error message=%s", _error.id().c_str(), _error.err_code(), _error.err_message().c_str());
        }
    }

    void WSClient::on_message(const String& id, websocketpp::connection_hdl hdl, client::message_ptr msg) {
        static int _counter = 0;
        _counter++;
        LOG_T(TAG, "on_message(%s): %s", id.c_str(), msg->get_payload().c_str());
        {
            MutexLocker _connection_map_guard(WSClient::connection_map_mutex_);
            if (connection_map_.count(id) > 0 && connection_map_[id]->owner_!=nullptr)
            {
                Message _message;
                _message.set_id(id);
                _message.set_payload(msg->get_payload());
                connection_map_[id]->owner_->SendEvent(Message::descriptor()->full_name(), _message.SerializeAsString());
            }
            if (connection_map_[id]->ref_count_==0)
            {
                String _reason;
                try
                {
                    if (connection_map_[id]->is_tls_)
                        connection_map_[id]->tls_client_.close(hdl, websocketpp::close::status::normal, _reason);
                    else
                        connection_map_[id]->client_.close(hdl, websocketpp::close::status::normal, _reason);
                }
                catch (const std::exception& e)
                {
                    LOG_E(TAG, "WSClient::on_message(%s), error message when close socket: %s", id.c_str(), e.what());
                }

                if (connection_map_[id]->owner_ != nullptr)
                {
                    Disconnected _disconnected;
                    _disconnected.set_id(id);
                    connection_map_[id]->owner_->SendEvent(Disconnected::descriptor()->full_name(), _disconnected.SerializeAsString());
                    the_last_owner_ = connection_map_[id]->owner_;
                    connection_map_.erase(id);
                }
            }
        }
    }
 
    void WSClient::on_close(const String& id, websocketpp::connection_hdl hdl) {
        MutexLocker _connection_map_guard(WSClient::connection_map_mutex_);
        if (connection_map_.count(id) > 0)
        {
            connection_map_[id]->hdl_ = hdl;
            websocketpp::lib::error_code ec;
            String _reason;
            try
            {
                if (connection_map_[id]->is_tls_)
                    connection_map_[id]->tls_client_.close(hdl, websocketpp::close::status::force_tcp_drop, _reason);
                else
                    connection_map_[id]->client_.close(hdl, websocketpp::close::status::force_tcp_drop, _reason);
            }
            catch (const std::exception& e)
            {
                LOG_E(TAG, "WSClient::on_close(%s), error message: %s", id.c_str(), e.what());
            }

            if (connection_map_[id]->owner_ != nullptr)
            {
                Disconnected _disconnected;
                _disconnected.set_id(id);
                connection_map_[id]->owner_->SendEvent(Disconnected::descriptor()->full_name(), _disconnected.SerializeAsString());
                the_last_owner_ = connection_map_[id]->owner_;
                connection_map_.erase(id);
            }
        }
    }

    void WSClient::on_fail(const String& id, websocketpp::connection_hdl hdl) {
        MutexLocker _connection_map_guard(WSClient::connection_map_mutex_);
        if (connection_map_.count(id) > 0)
        {
            connection_map_[id]->hdl_ = hdl;
            websocketpp::lib::error_code ec;
            Subscribe::Error _error;
            _error.set_id(id);
            _error.set_err_code(-2);
            _error.set_err_message("Something fails.");
            LOG_E(TAG, "WSClient.on_fail(%s) error code=%d, error message=%s", _error.id().c_str(), _error.err_code(), _error.err_message().c_str());
            connection_map_[id]->owner_->SendEvent(Subscribe::Error::descriptor()->full_name(), _error.SerializeAsString());
        }
    }

    /// TLS Initialization handler
    /**
     * WebSocket++ core and the Asio Transport do not handle TLS context creation
     * and setup. This callback is provided so that the end user can set up their
     * TLS context using whatever settings make sense for their application.
     *
     * As Asio and OpenSSL do not provide great documentation for the very common
     * case of connect and actually perform basic verification of server certs this
     * example includes a basic implementation (using Asio and OpenSSL) of the
     * following reasonable default settings and verification steps:
     *
     * - Disable SSLv2 and SSLv3
     * - Load trusted CA certificates and verify the server cert is trusted.
     * - Verify that the hostname matches either the common name or one of the
     *   subject alternative names on the certificate.
     *
     * This is not meant to be an exhaustive reference implimentation of a perfect
     * TLS client, but rather a reasonable starting point for building a secure
     * TLS encrypted WebSocket client.
     *
     * If any TLS, Asio, or OpenSSL experts feel that these settings are poor
     * defaults or there are critically missing steps please open a GitHub issue
     * or drop a line on the project mailing list.
     *
     * Note the bundled CA cert ca-chain.cert.pem is the CA cert that signed the
     * cert bundled with echo_server_tls. You can use print_client_tls with this
     * CA cert to connect to echo_server_tls as long as you use /etc/hosts or
     * something equivilent to spoof one of the names on that cert
     * (websocketpp.org, for example).
     */
    WSClient::context_ptr WSClient::on_tls_init(const String& id, const String& hostname, const char* cert, websocketpp::connection_hdl hdl) {
        context_ptr ctx = websocketpp::lib::make_shared<asio::ssl::context>(asio::ssl::context::sslv23);
        try {
            ctx->set_options(asio::ssl::context::default_workarounds |
                asio::ssl::context::no_sslv2 |
                asio::ssl::context::no_sslv3 |
                asio::ssl::context::single_dh_use);

            const bool _verify_none = false;
            if (_verify_none)
                ctx->set_verify_mode(asio::ssl::verify_none);
            else
                ctx->set_verify_mode(asio::ssl::verify_peer);
            ctx->set_verify_callback(bind( &verify_certificate, hostname.c_str(), ::_1, ::_2));

            // Here we load the CA certificates of all CA's that this client trusts.
            //ctx->load_verify_file("ca-chain.cert.pem");
            //ctx->load_verify_file("websocketpp.org.cer");
            //ctx->load_verify_file("cert/www.aben168.tw_35966947699CDAB158C8E04740CE09BD.cer");
            if (!_verify_none)
                ctx->load_verify_file(cert);
        }
        catch (std::exception& e) {
            LOG_E(TAG, "Exception from on_tls_init: %s", e.what());
        }
        return ctx;
    }

    void WSClient::on_pong(const String& id, websocketpp::connection_hdl hdl, std::string payload) {
        MutexLocker _connection_map_guard(WSClient::connection_map_mutex_);
        if (connection_map_[id]->ref_count_==0)
        {
            String _reason;
            try
            {
                if (connection_map_[id]->is_tls_)
                    connection_map_[id]->tls_client_.close(hdl, websocketpp::close::status::normal, _reason);
                else
                    connection_map_[id]->client_.close(hdl, websocketpp::close::status::normal, _reason);
            }
            catch (const std::exception& e)
            {
                LOG_E(TAG, "WSClient::on_pong(%s), error message when close socket: %s", id.c_str(), e.what());
            }

            if (connection_map_[id]->owner_ != nullptr)
            {
                Disconnected _disconnected;
                _disconnected.set_id(id);
                connection_map_[id]->owner_->SendEvent(Disconnected::descriptor()->full_name(), _disconnected.SerializeAsString());
                the_last_owner_ = connection_map_[id]->owner_;
                connection_map_.erase(id);
            }
        }
    }

    void WSClient::run()
    {
        io_service_.run();
        LOG_I(TAG, "leave connection thread!!");
        {
            MutexLocker _connection_map_guard(WSClient::connection_map_mutex_);
            if (connection_map_.size() > 0)
            {
                if (connection_map_.begin()->second->owner_ != nullptr)
                {
                    LOG_I(TAG, "WSClient.run() reset all by the map");
                    ResetAll _reset_all;
                    connection_map_.begin()->second->owner_->SendEvent(ResetAll::descriptor()->full_name(), _reset_all.SerializeAsString());
                }
                else
                {
                    LOG_W(TAG, "WSClient.run() reset all by the map but owner is nullptr");
                }
                connection_map_.clear();
            }
            else if (the_last_owner_ != nullptr)
            {
                LOG_I(TAG, "WSClient.run() reset all by the last owner");
                ResetAll _reset_all;
                the_last_owner_->SendEvent(ResetAll::descriptor()->full_name(), _reset_all.SerializeAsString());
            }
        }
        {
            MutexLocker _thread_guard(WSClient::thread_mutex_);
            if (asio_thread_ != nullptr)
            {
                asio_thread_.get()->detach();
                asio_thread_ = nullptr;
            }
        }
    }

    websocketpp::lib::asio::io_service WSClient::io_service_;
    Mutex WSClient::connection_map_mutex_;
    Mutex WSClient::thread_mutex_;
    Map<String, Obj<WSClient::ConnectionInfo>> WSClient::connection_map_;
    Obj<std::thread> WSClient::asio_thread_ = nullptr;
    WSClient* WSClient::the_last_owner_ = nullptr;

 	WSClient::WSClient(BioSys::IBiomolecule* owner)
		:RNA(owner, "WSClient", this)
	{
		init();
	}

	WSClient::~WSClient()
	{
        bool _the_last_one = true;
        {
            MutexLocker _connection_map_guard(WSClient::connection_map_mutex_);
            for (auto itr = connection_map_.begin(); itr != connection_map_.end(); itr++)
            {
                if (itr->second->owner_ == this)
                {
                    if (!itr->second->hdl_.expired())
                    {
                        if (itr->second->ref_count_ > 0)
                        {
                            itr->second->ref_count_ = 0;
                            itr->second->owner_ = nullptr;
                            if (itr->second->is_tls_)
                                itr->second->tls_client_.ping(itr->second->hdl_, "ping");   // for triggering on_message
                            else
                                itr->second->client_.ping(itr->second->hdl_, "ping");   // for triggering on_message
                        }
                    }
                }
                else
                {
                    _the_last_one = false;
                }
            }
        }
        {
            MutexLocker _thread_guard(WSClient::thread_mutex_);
            if (_the_last_one && asio_thread_ != nullptr)
            {
                //io_service_.stop();
                if (asio_thread_->joinable())
                    asio_thread_->join();
                asio_thread_ = nullptr;
            }
        }
    }

	void WSClient::OnEvent(const DynaArray& name)
	{
		USING_BIO_NAMESPACE
        const bool CLOSE_SOCKET = true;
        const String& _name = name.str();
		switch (hash(_name))
		{
        case "WSClient.Subscribe"_hash:
        {
            String _id = ReadValue<String>(_name + ".id");
            String _request = ReadValue<String>(_name + ".request");
            if (_id == "")
                _id = uuids::to_string(uuids::uuid_system_generator()());
            try {
                bool _is_starting = false;
                {
                    MutexLocker _connection_map_guard(WSClient::connection_map_mutex_);
                    if (connection_map_.count(_id) > 0)
                    {
                        //auto& _request_queue = connection_map_[_id]->request_queue_;
                        //_request_queue.push_back(std::make_pair(_request, !CLOSE_SOCKET));
                        if (!connection_map_[_id]->hdl_.expired())
                        {
                            websocketpp::lib::error_code ec;
                            //client::connection_ptr con = _client.get_connection(_uri, ec);
                            //connection_map_[_id]->client_.send(con, _request, websocketpp::frame::opcode::text, ec);
                            if (connection_map_[_id]->is_tls_)
                                connection_map_[_id]->tls_client_.send(connection_map_[_id]->hdl_, _request, websocketpp::frame::opcode::text, ec);
                            else
                                connection_map_[_id]->client_.send(connection_map_[_id]->hdl_, _request, websocketpp::frame::opcode::text, ec);
                            LOG_I(TAG, "WSClient.Subscribe(%s) send request: %s", _id.c_str(), _request.c_str());
                            if (ec) 
                            {
                                LOG_W(TAG, "Send request: %s\nfailed because: %s", _request.c_str(), ec.message().c_str());
                                Subscribe::Error _error;
                                _error.set_id(_id);
                                _error.set_err_code(-1);
                                _error.set_err_message(ec.message());
                                SendEvent(Subscribe::Error::descriptor()->full_name(), _error.SerializeAsString());
                            }
                            else
                            {
                                Subscribe::Result _result;
                                _result.set_id(_id);
                                _result.set_first_one(false);
                                SendEvent(Subscribe::Result::descriptor()->full_name(), _result.SerializeAsString());
                                connection_map_[_id]->ref_count_++;
                            }
                        }
                    }
                    else
                    {
                        String _uri = ReadValue<String>(_name + ".uri");
                        String _hostname = ReadValue<String>(_name + ".hostname");
                        String _cert_filename = GetRootPath() + ReadValue<String>(_name + ".cert_filename");
                        connection_map_.insert(std::make_pair(_id, Obj<ConnectionInfo>(new ConnectionInfo(this))));
                        if (connection_map_.size() == 1)
                        {
                            _is_starting = true;
                        }
                        if (_cert_filename.size() > GetRootPath().size())
                        {
                            connection_map_[_id]->is_tls_ = true;
                            tls_client& _client = connection_map_[_id]->tls_client_;
                            init_connection(_client, _id, _request, _hostname, _cert_filename, _uri);
                        }
                        else
                        {
                            connection_map_[_id]->is_tls_ = false;
                            client& _client = connection_map_[_id]->client_;
                            init_connection(_client, _id, _request, _hostname, "", _uri);
                        }
                    }
                }

                // Start the ASIO io_service run loop
                // this will cause a single connection to be made to the server. client_.run()
                // will exit when this connection is closed.
                if (_is_starting)
                {
                    io_service_.reset();
                    //io_service_.run();
                    LOG_I(TAG, "create connection thread");
                    {
                        MutexLocker _thread_guard(WSClient::thread_mutex_);
                        if (asio_thread_ != nullptr)
                        {
                            asio_thread_.get()->detach();
                            asio_thread_ = nullptr;
                        }
                        asio_thread_ = Obj<std::thread>(new std::thread(run));
                    }
                    LOG_I(TAG, "connection thread created");
                }
            }
            catch (websocketpp::exception const& e) {
                LOG_E(TAG, "Exception: %s", e.what());
                Subscribe::Error _error;
                _error.set_id(_id);
                _error.set_err_code(-3);
                _error.set_err_message(e.what());
                SendEvent(Subscribe::Error::descriptor()->full_name(), _error.SerializeAsString());
            }
            break;
        }
        case "WSClient.Unsubscribe"_hash:
        {
            String _id = ReadValue<String>(_name + ".id");
            //String _uri = ReadValue<String>(_name + ".uri");
            String _request = ReadValue<String>(_name + ".request");
            try {
                bool _is_stopping = false;
                {
                    MutexLocker _connection_map_guard(WSClient::connection_map_mutex_);
                    if (connection_map_.count(_id) > 0)
                    { 
                        websocketpp::lib::error_code ec;
                        //client::connection_ptr con = _client.get_connection(_uri, ec);
                        //_client.send(con->get_handle(), _request, websocketpp::frame::opcode::text, ec);
                        if (!_request.empty())
                        {
                            LOG_I(TAG, "WSClient.Unsubscribe(%s) send request: %s", _id.c_str(), _request.c_str());
                            if (connection_map_[_id]->is_tls_)
                            {
                                connection_map_[_id]->tls_client_.send(connection_map_[_id]->hdl_, _request, websocketpp::frame::opcode::text, ec);
                            }
                            else
                            {
                                connection_map_[_id]->client_.send(connection_map_[_id]->hdl_, _request, websocketpp::frame::opcode::text, ec);
                            }
                        }
                        connection_map_[_id]->ref_count_--;
                        bool _the_last_one = false;
                        if (connection_map_[_id]->ref_count_ <= 1)      // 1 for login
                        {
                            connection_map_[_id]->ref_count_ = 0;
                            _the_last_one = true;
                        }
                        if (connection_map_[_id]->owner_ != nullptr)
                        {
                            LOG_I(TAG, "WSClient.Unsubscribe(%s) ref_count= %d", _id.c_str(), connection_map_[_id]->ref_count_);
                            Unsubscribe::Result _unsubscribe_result;
                            _unsubscribe_result.set_id(_id);
                            _unsubscribe_result.set_last_one(_the_last_one);
                            connection_map_[_id]->owner_->RaiseEvent(Unsubscribe::Result::descriptor()->full_name(), _unsubscribe_result.SerializeAsString());
                        }
                        if (_request.empty() && _the_last_one)
                        {
                            String _reason;
                            try
                            {
                                if (connection_map_[_id]->is_tls_)
                                    connection_map_[_id]->tls_client_.close(connection_map_[_id]->hdl_, websocketpp::close::status::normal, _reason);
                                else
                                    connection_map_[_id]->client_.close(connection_map_[_id]->hdl_, websocketpp::close::status::normal, _reason);
                            }
                            catch (const std::exception& e)
                            {
                                LOG_E(TAG, "WSClient.Unsubscribe(%s), error message when close socket: %s", _id.c_str(), e.what());
                            }
                            the_last_owner_ = connection_map_[_id]->owner_;
                            connection_map_.erase(_id);
                        }
                        //if (connection_map_.size() == 0)
                        //    _is_stopping = true;
                    }
                }
                //if (_is_stopping)
                //{
                //    io_service_.stop();
                //    if (asio_thread_->joinable())
                //        asio_thread_->join();
                //}
            }
            catch (websocketpp::exception const& e) {
                LOG_E(TAG, "Exception: %s", e.what());
            }
            break;
		}
        case "WSClient.PlaceOrder"_hash:
        {
            String _id = ReadValue<String>(_name + ".id");
            String _request = ReadValue<String>(_name + ".request");
            if (_id == "")
                _id = uuids::to_string(uuids::uuid_system_generator()());
            try {
                bool _is_starting = false;
                {
                    MutexLocker _connection_map_guard(WSClient::connection_map_mutex_);
                    if (connection_map_.count(_id) > 0)
                    {
                        //auto& _request_queue = connection_map_[_id]->request_queue_;
                        //_request_queue.push_back(std::make_pair(_request, !CLOSE_SOCKET));
                        if (!connection_map_[_id]->hdl_.expired())
                        {
                            websocketpp::lib::error_code ec;
                            //client::connection_ptr con = _client.get_connection(_uri, ec);
                            //connection_map_[_id]->client_.send(con, _request, websocketpp::frame::opcode::text, ec);
                            if (connection_map_[_id]->is_tls_)
                                connection_map_[_id]->tls_client_.send(connection_map_[_id]->hdl_, _request, websocketpp::frame::opcode::text, ec);
                            else
                                connection_map_[_id]->client_.send(connection_map_[_id]->hdl_, _request, websocketpp::frame::opcode::text, ec);
                            LOG_I(TAG, "WSClient.PlaceOrder(%s) send request: %s", _id.c_str(), _request.c_str());
                            if (ec)
                            {
                                LOG_W(TAG, "WSClient.PlaceOrder(%s): request: %s\nfailed because: %s", _id.c_str(), _request.c_str(), ec.message().c_str());
                                PlaceOrder::Error _error;
                                _error.set_id(_id);
                                _error.set_err_code(-1);
                                _error.set_err_message(ec.message());
                                SendEvent(PlaceOrder::Error::descriptor()->full_name(), _error.SerializeAsString());
                            }
                            else
                            {
                                PlaceOrder::Result _result;
                                _result.set_id(_id);
                                _result.set_first_one(false);
                                SendEvent(PlaceOrder::Result::descriptor()->full_name(), _result.SerializeAsString());
                            }
                        }
                    }
                    else
                    {
                        LOG_W(TAG, "WSClient.PlaceOrder(%s): channel not ready, request: %s\n", _id.c_str(), _request.c_str());
                        PlaceOrder::Error _error;
                        _error.set_id(_id);
                        _error.set_err_code(-2);
                        _error.set_err_message("channel not ready");
                        SendEvent(PlaceOrder::Error::descriptor()->full_name(), _error.SerializeAsString());
                    }
                }

                // Start the ASIO io_service run loop
                // this will cause a single connection to be made to the server. client_.run()
                // will exit when this connection is closed.
                if (_is_starting)
                {
                    io_service_.reset();
                    //io_service_.run();
                    LOG_I(TAG, "create connection thread");
                    {
                        MutexLocker _thread_guard(WSClient::thread_mutex_);
                        if (asio_thread_ != nullptr)
                        {
                            asio_thread_.get()->detach();
                            asio_thread_ = nullptr;
                        }
                        asio_thread_ = Obj<std::thread>(new std::thread(run));
                    }
                    LOG_I(TAG, "connection thread created");
                }
            }
            catch (websocketpp::exception const& e) {
                LOG_E(TAG, "Exception: %s", e.what());
                PlaceOrder::Error _error;
                _error.set_id(_id);
                _error.set_err_code(-3);
                _error.set_err_message(e.what());
                SendEvent(PlaceOrder::Error::descriptor()->full_name(), _error.SerializeAsString());
            }
            break;
        }
        case "WSClient.Keepalive"_hash:
        {
            MutexLocker _connection_map_guard(WSClient::connection_map_mutex_);
            for (auto& elem : connection_map_)
            {
                if (elem.second->owner_ == this)
                {
                    LOG_I(TAG, "[%s] keepalive", elem.first.c_str());
                    //auto& _request_queue = elem.second->request_queue_;
                    //_request_queue.push_back(std::make_pair("ping", !CLOSE_SOCKET));
                    if (elem.second->is_tls_)
                        elem.second->tls_client_.ping(elem.second->hdl_, "ping");
                    else
                        elem.second->client_.ping(elem.second->hdl_, "ping");
                    //websocketpp::lib::error_code ec;
                    //elem.second->client_.send(elem.second->hdl_, "ping", websocketpp::frame::opcode::text, ec);
                }
            }
            break;
        }
        default:
			break;
		}
	}

    template <typename T>
    void WSClient::init_connection(T& client_instance, const String& id, const String& request, const String& hostname, const String& cert_filename, const String& uri)
    {
        // Set logging to be pretty verbose (everything except message payloads)
        client_instance.set_access_channels(websocketpp::log::alevel::all);
        client_instance.clear_access_channels(websocketpp::log::alevel::frame_header);
        client_instance.clear_access_channels(websocketpp::log::alevel::frame_payload);
        client_instance.set_error_channels(websocketpp::log::elevel::all);

        // Initialize ASIO
        client_instance.init_asio(&io_service_);

        // Register our message handler
        client_instance.set_open_handler(bind(&on_connected, id, request, ::_1));
        client_instance.set_message_handler(bind(&on_message, id, ::_1, ::_2));
        init_tls(client_instance, id, hostname, cert_filename);
        client_instance.set_pong_handler(bind(&on_pong, id, ::_1, ::_2));
        client_instance.set_close_handler(bind(&on_close, id, ::_1));
        client_instance.set_fail_handler(bind(&on_fail, id, ::_1));

        websocketpp::lib::error_code ec;
        typename T::connection_ptr con = client_instance.get_connection(uri, ec);
        if (ec)
        {
            LOG_W(TAG, "[%s] could not create connection to \"%s\" because: %s", id.c_str(), uri.c_str(), ec.message().c_str());
            Subscribe::Error _error;
            _error.set_id(id);
            _error.set_err_code(-2);
            _error.set_err_message(ec.message());
            SendEvent(Subscribe::Error::descriptor()->full_name(), _error.SerializeAsString());
        }
        else
        {
            // Note that connect here only requests a connection. No network messages are
            // exchanged until the event loop starts running in the next line.
            client_instance.connect(con);
            client_instance.get_alog().write(websocketpp::log::alevel::app, "Connecting to " + uri);
        }
    }

    inline void WSClient::init_tls(tls_client& client_instance, const String& id, const String& hostname, const String& cert_filename)
    {
        client_instance.set_tls_init_handler(bind(&on_tls_init, id, hostname, cert_filename.c_str(), ::_1));
    }
}