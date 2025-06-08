#pragma once
#include "RNA.h"
#include <websocketpp/config/asio_client.hpp>
#include <websocketpp/client.hpp>

namespace WSClient
{

class WSClient : public BioSys::RNA
{
	typedef websocketpp::client<websocketpp::config::asio_tls_client> tls_client;
	typedef websocketpp::client<websocketpp::config::asio_client> client;
	typedef websocketpp::lib::shared_ptr<websocketpp::lib::asio::ssl::context> context_ptr;
	struct ConnectionInfo
	{
		ConnectionInfo() { 
			owner_ = nullptr; 
			ref_count_ = 0;
		};
		ConnectionInfo(WSClient* owner) { 
			owner_ = owner; 
			ref_count_ = 0;
		};
		WSClient* owner_;
		tls_client tls_client_;
		client client_;
		bool is_tls_;
		websocketpp::connection_hdl hdl_;
		int ref_count_;
	};
public:
	PUBLIC_API WSClient(BioSys::IBiomolecule* owner);
	PUBLIC_API virtual ~WSClient();

protected:
	virtual void OnEvent(const DynaArray& name);

private:
	static void on_connected(const String& id, const String& request, websocketpp::connection_hdl hdl);
	static void on_close(const String& id, websocketpp::connection_hdl hdl);
	static void on_fail(const String& id, websocketpp::connection_hdl hdl);
	static void on_message(const String& id, websocketpp::connection_hdl hdl, client::message_ptr msg);
	static context_ptr on_tls_init(const String& id, const String& hostname, const char* cert, websocketpp::connection_hdl hdl);
	static void on_pong(const String& id, websocketpp::connection_hdl hdl, std::string payload);
	static bool verify_subject_alternative_name(const char* hostname, X509* cert);
	static bool verify_common_name(char const* hostname, X509* cert);
	static bool verify_certificate(const char* hostname, bool preverified, asio::ssl::verify_context& ctx);
	static void run();

private:
	static websocketpp::lib::asio::io_service io_service_;
	static Map<String, Obj<ConnectionInfo>> connection_map_;
	static Mutex connection_map_mutex_;
	static Obj<std::thread> asio_thread_;
	static Mutex thread_mutex_;
	static WSClient* the_last_owner_;

private:
	template <typename T> void init_connection(T& client_instance, const String& id, const String& request, const String& hostname, const String& cert_filename, const String& uri);
	void init_tls(tls_client& client_instance, const String& id, const String& hostname, const String& cert_filename);
	void init_tls(client& client_instance, const String& id, const String& hostname, const String& cert_filename) {};
};

}