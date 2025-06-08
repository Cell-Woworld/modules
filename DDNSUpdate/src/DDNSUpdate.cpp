#include "DDNSUpdate.h"
#include "../proto/DDNSUpdate.pb.h"
#include <httplib.h>

#ifdef _WIN32
#include <winsock.h>
#else
#include <sys/socket.h>
#endif

#define TAG "DDNSUpdate"

namespace DDNSUpdate
{

#ifndef STATIC_API
	extern "C" PUBLIC_API BioSys::RNA* CreateInstance(BioSys::IBiomolecule* owner)
	{
		return new DDNSUpdate(owner);
	}
#endif

	DDNSUpdate::DDNSUpdate(BioSys::IBiomolecule* owner)
		:RNA(owner, "DDNSUpdate", this)
	{
		init();
	}

	DDNSUpdate::~DDNSUpdate()
	{
	}

	void DDNSUpdate::OnEvent(const DynaArray& name)
	{
		USING_BIO_NAMESPACE
		const String& _name = name.str();
		switch (hash(_name))
		{
		case "DDNSUpdate.nsupdate_info.Update"_hash:
		{
			using namespace httplib;
			String _host = ReadValue<String>(_name + ".host");
			String _path = ReadValue<String>(_name + ".path");
			String _id = ReadValue<String>(_name + ".id");
			String _password = ReadValue<String>(_name + ".password");
			if (_host == "")
				return;
			if (_path == "")
				_path = "/";

			SSLClient cli(_host);
			// Digest Authentication
			//cli.set_digest_auth(_id.c_str(), _password.c_str());
			// Basic Authentication
			cli.set_basic_auth(_id.c_str(), _password.c_str());
			auto res = cli.Get(_path.c_str());
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
				else
#endif
				{
					SendEvent("DDNSUpdate.Error.nsupdate_info");
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