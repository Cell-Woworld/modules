#include "FirebasePush.h"
#include "../proto/FirebasePush.pb.h"
#include <httplib.h>
#include <regex>
#include "nlohmann/json.hpp"

#ifdef _WIN32
#include <winsock.h>
#else
#include <sys/socket.h>
#endif

#include <iostream>
#include <jwt-cpp/jwt.h>

#define TAG "FirebasePush"

namespace FirebasePush
{

#ifndef STATIC_API
	extern "C" PUBLIC_API BioSys::RNA* CreateInstance(BioSys::IBiomolecule* owner)
	{
		return new FirebasePush(owner);
	}
#endif

	FirebasePush::FirebasePush(BioSys::IBiomolecule* owner)
		:RNA(owner, "FirebasePush", this)
	{
		init();
	}

	FirebasePush::~FirebasePush()
	{
	}

	void FirebasePush::OnEvent(const DynaArray& name)
	{
		USING_BIO_NAMESPACE
		const String& _name = name.str();
		switch (hash(_name))
		{
		case "FirebasePush.Send"_hash:
		{
			using namespace httplib;
			using namespace nlohmann;

			String _project_id = ReadValue<String>(_name + ".projectId");
			String _issuer = ReadValue<String>(_name + ".issuer");
			String _private_key = ReadValue<String>(_name + ".token");
			String _scope_url = "https://www.googleapis.com/auth/firebase.messaging";
			String google_oauth_url = "oauth2.googleapis.com";
			String _access_token;
			{
				_private_key = std::regex_replace(_private_key, std::regex(R"(\\n)"), "\n");
				SSLClient cli(google_oauth_url);
				auto _token = jwt::create()
					.set_issuer(_issuer==""?"admin": _issuer)
					.set_type("JWT")
					.set_id("FirebasePush")
					.set_audience("https://" + google_oauth_url + "/token")
					.set_issued_now()
					.set_expires_in(std::chrono::seconds{ 60 })
					.set_payload_claim("scope", jwt::claim(_scope_url))
					.sign(jwt::algorithm::rs256("", _private_key, "", ""));
				json _body;
				_body["grant_type"] = "urn:ietf:params:oauth:grant-type:jwt-bearer";
				_body["assertion"] = _token;
				auto res = cli.Post("/token", _body.dump(), "application/json");
				if (!res)
				{
					auto result = cli.get_openssl_verify_result();
					if (result) {
						LOG_E(TAG, "OAuth2: %s", X509_verify_cert_error_string(result));
					}
					else {
						LOG_E(TAG, "OAuth2: %s", res->body.c_str());
					}
					break;
				}
				else
				{
					try
					{
						json _result = json::parse(res->body);
						_access_token = _result["access_token"].get<String>();
					}
					catch (const std::exception& e)
					{
						LOG_E(TAG, "Error when parsing \"%s\" as JSON, exception: %s", res->body.c_str(), e.what());
						break;
					}
				}
			}
			{
				SSLClient cli("fcm.googleapis.com");
				Headers headers = {
					{"Authorization", ((String)"Bearer " + _access_token).c_str()}
				};
				json _body, _notification;
				_notification["title"] = ReadValue<String>(_name + ".title");
				_notification["body"] = ReadValue<String>(_name + ".content");
				//_notification["sound"] = "default";
				_notification["image"] = "";
				_body["message"] = json::object();
				_body["message"]["token"] = ReadValue<String>(_name + ".targetId");
				_body["message"]["notification"] = _notification;
				{
					json _apns = json::parse(
						"{\"payload\": {				\
						\"aps\": {					\
							\"sound\": \"default\", \
							\"badge\" : 1,			\
							\"content-available\" : 1}}}"
					);
					_body["message"]["apns"] = _apns;
				}
				LOG_I(TAG, "*************** output content: ***************\n%s\n******************************", _body.dump().c_str());
				auto res = cli.Post((String("/v1/projects/") + _project_id + "/messages:send").c_str(), headers, _body.dump(), "application/json");
				if (res) {
					std::cout << res->status << std::endl;
					std::cout << res->get_header_value("Content-Type") << std::endl;
					std::cout << res->body << std::endl;
				}
				else {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
					auto result = cli.get_openssl_verify_result();
					if (result) {
						std::cout << "verify error: " << X509_verify_cert_error_string(result) << std::endl;
					}
#endif
				}
			}
			break;
		}
		default:
			LOG_E(TAG, "Unsupported action: %s", _name.c_str());
			break;
		}
	}

}