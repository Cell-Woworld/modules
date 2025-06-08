#include "RestfulAgent.h"
#include "../proto/RestfulAgent.pb.h"
#include "nlohmann/json.hpp"
#include "httplib.h"

#ifdef _WIN32
#include <winsock.h>
#pragma comment(lib, "wsock32.lib")
#define close closesocket
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#define TAG "RestfulAgent"

using json = nlohmann::json;

namespace RestfulAgent
{

#ifndef STATIC_API
	extern "C" PUBLIC_API BioSys::RNA* CreateInstance(BioSys::IBiomolecule* owner)
	{
		return new RestfulAgent(owner);
	}
#endif

	RestfulAgent::RestfulAgent(BioSys::IBiomolecule* owner)
		:RNA(owner, "RestfulAgent", this)
	{
		init();
	}

	RestfulAgent::~RestfulAgent()
	{
	}

	void RestfulAgent::OnEvent(const DynaArray& name)
	{
		USING_BIO_NAMESPACE
		using namespace httplib;
		const String& _name = name.str();
		switch (hash(_name))
		{
		case "RestfulAgent.Get"_hash:
		{
			String _id = ReadValue<String>(_name + ".id");
			String _host = ReadValue<String>(_name + ".host");
			String _request = ReadValue<String>(_name + ".request");
			String _header_str = ReadValue<String>(_name + ".headers");
			json _header_set;
			try
			{
				_header_set = json::parse(_header_str);
			}
			catch (const std::exception& e)
			{
				LOG_E(TAG, "RestfulAgent::Get() Fail to json::parse header: %s, exception: %s", _header_str.c_str(), e.what());
			}
			SSLClient cli(_host);
			Headers _headers = {
				{"Content-Type", "application/json"},
			};
			for (const auto item : _header_set.items())
			{
				_headers.insert(std::make_pair(item.key(), item.value()));
			}
			httplib::Result res = cli.Get(_request, _headers);
			if (res->status == 200) {
				Get::Result _result;
				_result.set_id(_id);
				_result.set_content(res->body);
				SendEvent(Get::Result::descriptor()->full_name(), _result.SerializeAsString());
			}
			else {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
				auto result = cli.get_openssl_verify_result();
				if (result)
				{
					Error _result;
					_result.set_id(_id);
					_result.set_err_code(res->status);
					_result.set_reason(res->reason);
					_result.set_err_message(X509_verify_cert_error_string(result));
					SendEvent(Error::descriptor()->full_name(), _result.SerializeAsString());
				}
				else
				{
					Error _result;
					_result.set_id(_id);
					_result.set_err_code(res->status);
					_result.set_reason(res->reason);
					_result.set_err_message(res->body);
					SendEvent(Error::descriptor()->full_name(), _result.SerializeAsString());
				}
#endif
			}
			break;
		}
		case "RestfulAgent.Post"_hash:
		{
			String _id = ReadValue<String>(_name + ".id");
			String _host = ReadValue<String>(_name + ".host");
			String _request = ReadValue<String>(_name + ".request");
			String _body = ReadValue<String>(_name + ".body");
			String _header_str = ReadValue<String>(_name + ".headers");
			json _header_set;
			try
			{
				_header_set = json::parse(_header_str);
			}
			catch (const std::exception& e)
			{
				LOG_E(TAG, "RestfulAgent::Post() Fail to json::parse header: %s, exception: %s", _header_str.c_str(), e.what());
			}
			SSLClient cli(_host);
			Headers _headers = {
				{"Content-Type", "application/json"},
			};
			for (const auto item : _header_set.items())
			{
				_headers.insert(std::make_pair(item.key(), item.value()));
			}
			httplib::Result res = cli.Post(_request, _headers, _body, "application/json");
			if (res->status == 200) {
				Post::Result _result;
				_result.set_id(_id);
				_result.set_content(res->body);
				SendEvent(Post::Result::descriptor()->full_name(), _result.SerializeAsString());
			}
			else {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
				auto result = cli.get_openssl_verify_result();
				if (result)
				{
					Error _result;
					_result.set_id(_id);
					_result.set_err_code(res->status);
					_result.set_reason(res->reason);
					_result.set_err_message(X509_verify_cert_error_string(result));
					SendEvent(Error::descriptor()->full_name(), _result.SerializeAsString());
				}
				else
				{
					Error _result;
					_result.set_id(_id);
					_result.set_err_code(res->status);
					_result.set_reason(res->reason);
					_result.set_err_message(res->body);
					SendEvent(Error::descriptor()->full_name(), _result.SerializeAsString());
				}
#endif
			}
			break;
		}
		default:
			break;
		}
	}

}