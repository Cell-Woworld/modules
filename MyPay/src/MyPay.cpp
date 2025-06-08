#include "MyPay.h"
#include "../proto/Payment.pb.h"
#include "httplib.h"

#ifdef _WIN32
#include <winsock.h>
#else
#include <sys/socket.h>
#endif

#define CBC 1
#define ECB 0
#define CTR 0

#include "tinyAES/aes.hpp"
#include "nlohmann/json.hpp"
#include "cbase64/include/cbase64/cbase64.h"
#include "UrlEncoder/Encoder.hpp"


#define TAG "MyPay"

namespace MyPay
{

#ifndef STATIC_API
	extern "C" PUBLIC_API BioSys::RNA* CreateInstance(BioSys::IBiomolecule* owner)
	{
		return new MyPay(owner);
	}
#endif

	MyPay::MyPay(BioSys::IBiomolecule* owner)
		:RNA(owner, "MyPay", this)
	{
		init();
	}

	MyPay::~MyPay()
	{
	}

	void MyPay::OnEvent(const DynaArray& name)
	{
		USING_BIO_NAMESPACE
		using namespace Payment;
		const String& _name = name.str();
		switch (BioSys::hash(_name))
		{
		case "Payment.Order"_hash:
		{
			using namespace nlohmann;
			String _svr_encrypt, _data_encrypt;
			{
				json _svr;
				_svr["service_name"] = "api";
				_svr["cmd"] = "api/orders";
				CopyAndPatch(_svr.dump(), _svr_encrypt);
			}
			{
				json _data;
				int _grand_total = 0;
				Array<String> _order_list = ReadValue<Array<String>>(name.str() + ".orderList");
				for (int i = 0; i < _order_list.size(); i++)
				{
					Order::ProductInfo _product_info;
					if (_order_list[i] != "" && _product_info.ParseFromString(_order_list[i]) == true)
					{
						_data[(String)"i_" + std::to_string(i) + "_id"] = _product_info.productid();
						_data[(String)"i_" + std::to_string(i) + "_name"] = _product_info.productname();
						_data[(String)"i_" + std::to_string(i) + "_cost"] = std::to_string((int)_product_info.price());
						_data[(String)"i_" + std::to_string(i) + "_amount"] = std::to_string(_product_info.amount());
						int _total = (int)(_product_info.price() * _product_info.amount());
						_data[(String)"i_" + std::to_string(i) + "_total"] = std::to_string(_total);
						_grand_total += _total;
					}
				}
				_data["cost"] = std::to_string(_grand_total);
				_data["store_uid"] = ReadValue<String>(name.str() + ".storeId");
				_data["pfn"] = ReadValue<String>(name.str() + ".choosePayment");;
				_data["user_id"] = ReadValue<String>(name.str() + ".userId");
				_data["ip"] = ReadValue<String>(name.str() + ".remoteIPAddress");;
				_data["item"] = std::to_string(_order_list.size());
				_data["order_id"] = ReadValue<String>(name.str() + ".orderId");
				if (ReadValue<String>(name.str() + ".repeatType") != "")
				{
					_data["regular"] = ReadValue<String>(name.str() + ".repeatType");
					_data["regular_total"] = ReadValue<String>(name.str() + ".totalRepeatCount");
					_data["group_id"] = ReadValue<String>(name.str() + ".groupId");
				}
				CopyAndPatch(_data.dump(), _data_encrypt);
			}

			typedef std::chrono::high_resolution_clock hrclock;
#if defined(_M_X64) || defined(__amd64__)
			unsigned long long _seed = hrclock::now().time_since_epoch().count();
			std::mt19937_64 _generator(_seed);
#else
			unsigned int _seed = (unsigned int)hrclock::now().time_since_epoch().count();
			std::mt19937 _generator(_seed);
#endif
			std::uniform_int_distribution<short> _distribution(0, 255);
			//std::uniform_int_distribution<short> _distribution('0', '0');
			uint8_t iv[16];
			for (int i = 0; i < 16; i++)
			{
				iv[i] = (uint8_t)_distribution(_generator);
			}

			String key = ReadValue<String>(name.str() + ".token1");
			struct AES_ctx ctx;
			AES_init_ctx_iv(&ctx, (const uint8_t*)key.data(), iv);

			String _svr_encoded, _data_encoded;
			{
				AES_CBC_encrypt_buffer(&ctx, (uint8_t*)_svr_encrypt.data(), (uint32_t)_svr_encrypt.size());
				String _svr_encrypt_combined = String((const char*)iv, sizeof(iv)) + String(_svr_encrypt.begin(), _svr_encrypt.end());
				Base64Encode(_svr_encrypt_combined, _svr_encoded);
			}
			{
				memcpy(iv, ctx.Iv, sizeof(iv));
				//memcpy(ctx.Iv, iv, sizeof(iv));
				AES_CBC_encrypt_buffer(&ctx, (uint8_t*)_data_encrypt.data(), (uint32_t)_data_encrypt.size());
				String _data_encrypt_combined = String((const char*)iv, sizeof(iv)) + String(_data_encrypt.begin(), _data_encrypt.end());
				Base64Encode(_data_encrypt_combined, _data_encoded);
			}
			Encoder _encoder;
			_svr_encoded = _encoder.UTF8UrlEncode(_svr_encoded);
			_data_encoded = _encoder.UTF8UrlEncode(_data_encoded);
			LOG_D(TAG, "svr_encoded = %s", _svr_encoded.c_str());
			LOG_D(TAG, "data_encoded = %s", _data_encoded.c_str());

			using namespace httplib;
			SSLClient cli(ReadValue<String>(name.str() + ".serviceHost"));
			//MultipartFormDataItems items = { 
			//	{"store_uid", "123456760004", "", ""},
			//	{"service", _svr_encoded, "", ""},
			//	{"encry_data", _data_encoded, "", ""}
			//};
			//auto res = cli.Post("/api/init", items);

			auto res = cli.Post("/api/init",
				(String)"store_uid=" + ReadValue<String>(name.str() + ".storeId") + "&service=" + _svr_encoded + "&encry_data=" + _data_encoded,
				"application/x-www-form-urlencoded");
			if (res) {
				std::cout << res->status << std::endl;
				std::cout << res->get_header_value("Content-Type") << std::endl;
				std::cout << res->body << std::endl;

				json _result = json::parse(res->body);
				if (_result["code"].get<String>() != "200")
				{
					Order::Error _order_error;
					_order_error.set_errcode(_result["code"].get<String>());
					_order_error.set_message(_result["msg"].get<String>());
					SendEvent(Order::Error::descriptor()->full_name(), _order_error.SerializeAsString());
				}
				else
				{
					Order::Result _order_result;
					if (_result["url"].is_string())
					{ 
						String _url = _result["url"].get<String>();
						_url = std::regex_replace(_url, std::regex("\\/"), "/");
						_order_result.set_url(_url);
					}
					_order_result.set_code(_result["code"].get<String>());
					if (_result["uid"].is_number())
					{
						_order_result.set_uid(std::to_string(_result["uid"].get<int>()));
					}
					if (_result["key"].is_string())
					{
						_order_result.set_key(_result["key"].get<String>());
					}
					SendEvent(Order::Result::descriptor()->full_name(), _order_result.SerializeAsString());
				}
			}
			else {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
				auto result = cli.get_openssl_verify_result();
				if (result) {
					std::cout << "verify error: " << X509_verify_cert_error_string(result) << ", key=" << key << std::endl;
				}
#endif
				Order::Error _order_error;
				_order_error.set_errcode("-1");
				_order_error.set_message((String)"OpenSSL verify error: " + X509_verify_cert_error_string(result));
				SendEvent(Order::Error::descriptor()->full_name(), _order_error.SerializeAsString());
			}

			break;
		}
		case "Payment.CheckOrder"_hash:
		{
			using namespace nlohmann;
			String _svr_encrypt, _data_encrypt;
			{
				json _svr;
				_svr["service_name"] = "api";
				_svr["cmd"] = "api/queryorder";
				CopyAndPatch(_svr.dump(), _svr_encrypt);
			}
			{
				json _data;
				_data["uid"] = ReadValue<String>(name.str() + ".uid");
				_data["key"] = ReadValue<String>(name.str() + ".key");
				CopyAndPatch(_data.dump(), _data_encrypt);
			}

			typedef std::chrono::high_resolution_clock hrclock;
#if defined(_M_X64) || defined(__amd64__)
			unsigned long long _seed = hrclock::now().time_since_epoch().count();
			std::mt19937_64 _generator(_seed);
#else
			unsigned int _seed = (unsigned int)hrclock::now().time_since_epoch().count();
			std::mt19937 _generator(_seed);
#endif
			std::uniform_int_distribution<short> _distribution(0, 255);
			uint8_t iv[16];
			for (int i = 0; i < 16; i++)
			{
				iv[i] = (uint8_t)_distribution(_generator);
			}

			String key = ReadValue<String>(name.str() + ".token1");
			struct AES_ctx ctx;
			AES_init_ctx_iv(&ctx, (const uint8_t*)key.data(), iv);

			String _svr_encoded, _data_encoded;
			{
				AES_CBC_encrypt_buffer(&ctx, (uint8_t*)_svr_encrypt.data(), (uint32_t)_svr_encrypt.size());
				String _svr_encrypt_combined = String((const char*)iv, sizeof(iv)) + String(_svr_encrypt.begin(), _svr_encrypt.end());
				Base64Encode(_svr_encrypt_combined, _svr_encoded);
			}
			{
				memcpy(iv, ctx.Iv, sizeof(iv));
				AES_CBC_encrypt_buffer(&ctx, (uint8_t*)_data_encrypt.data(), (uint32_t)_data_encrypt.size());
				String _data_encrypt_combined = String((const char*)iv, sizeof(iv)) + String(_data_encrypt.begin(), _data_encrypt.end());
				Base64Encode(_data_encrypt_combined, _data_encoded);
			}
			Encoder _encoder;
			_svr_encoded = _encoder.UTF8UrlEncode(_svr_encoded);
			_data_encoded = _encoder.UTF8UrlEncode(_data_encoded);
			LOG_D(TAG, "svr_encoded = %s", _svr_encoded.c_str());
			LOG_D(TAG, "data_encoded = %s", _data_encoded.c_str());

			using namespace httplib;
			SSLClient cli(ReadValue<String>(name.str() + ".serviceHost"));

			const char SYS_ERROR[] = "-1";
			auto res = cli.Post("/api/init",
				(String)"store_uid=" + ReadValue<String>(name.str() + ".storeId") + "&service=" + _svr_encoded + "&encry_data=" + _data_encoded,
				"application/x-www-form-urlencoded");
			if (res) {
				std::cout << res->status << std::endl;
				std::cout << res->get_header_value("Content-Type") << std::endl;
				std::cout << res->body << std::endl;
				json _json_result = json::parse(res->body);
				String _ret_code = "";
				String _ret_msg = "";
				if (!_json_result["prc"].is_null())
				{
					_ret_code = _json_result["prc"].get<String>();
					_ret_msg = _json_result["retmsg"].get<String>();
				}
				else
				{
					_ret_code = SYS_ERROR;
					_ret_msg = _json_result["msg"].get<String>();
				}
				if (_ret_code != "250" && _ret_code != "600")
				{
					CheckOrder::Error _error;
					_error.set_errcode(_ret_code);
					_error.set_message(_ret_msg);
					if (_ret_code != SYS_ERROR)
					{
						_error.set_uid(std::to_string(_json_result["uid"].get<int>()));
						_error.set_key(_json_result["key"].get<String>());
						_error.set_orderid(_json_result["order_id"].get<String>());
						_error.set_userid(_json_result["user_id"].get<String>());
						_error.set_pfn(_json_result["pfn"].get<String>());
						_error.set_finishtime(_json_result["finishtime"].get<String>());
						_error.set_cost(std::to_string(_json_result["cost"].get<int>()));
						_error.set_paymentname(_json_result["payment_name"].get<String>());
					}
					SendEvent(CheckOrder::Error::descriptor()->full_name(), _error.SerializeAsString());
				}
				else
				{
					CheckOrder::Result _result;
					_result.set_code(_ret_code);
					_result.set_message(_ret_msg);
					_result.set_uid(std::to_string(_json_result["uid"].get<int>()));
					_result.set_key(_json_result["key"].get<String>());
					_result.set_orderid(_json_result["order_id"].get<String>());
					_result.set_userid(_json_result["user_id"].get<String>());
					_result.set_pfn(_json_result["pfn"].get<String>());
					_result.set_finishtime(_json_result["finishtime"].get<String>());
					_result.set_cost(std::to_string(_json_result["actual_cost"].get<int>()));
					_result.set_paymentname(_json_result["payment_name"].get<String>());
					SendEvent(CheckOrder::Result::descriptor()->full_name(), _result.SerializeAsString());
				}
			}
			else
			{
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
				auto result = cli.get_openssl_verify_result();
				if (result) {
					std::cout << "verify error: " << X509_verify_cert_error_string(result) << ", key=" << key << std::endl;
				}
#endif
				CheckOrder::Error _error;
				_error.set_errcode(SYS_ERROR);
				_error.set_message((String)"OpenSSL verify error: " + X509_verify_cert_error_string(result));
				SendEvent(CheckOrder::Error::descriptor()->full_name(), _error.SerializeAsString());
			}
			break;
		}
		case "Payment.CancelOrder"_hash:
		{
			using namespace nlohmann;
			String _svr_encrypt, _data_encrypt;
			{
				json _svr;
				_svr["service_name"] = "api";
				_svr["cmd"] = "api/disabledirectdebit";
				CopyAndPatch(_svr.dump(), _svr_encrypt);
			}
			{
				json _data;
				_data["store_uid"] = ReadValue<String>(name.str() + ".storeId");
				_data["order_id"] = ReadValue<String>(name.str() + ".orderId");
				_data["group_id"] = ReadValue<String>(name.str() + ".groupId");
				_data["stop_time"] = ReadValue<String>(name.str() + ".cancelTime");
				_data["stop_reason"] = ReadValue<String>(name.str() + ".cancelReason");
				CopyAndPatch(_data.dump(), _data_encrypt);
			}

			typedef std::chrono::high_resolution_clock hrclock;
#if defined(_M_X64) || defined(__amd64__)
			unsigned long long _seed = hrclock::now().time_since_epoch().count();
			std::mt19937_64 _generator(_seed);
#else
			unsigned int _seed = (unsigned int)hrclock::now().time_since_epoch().count();
			std::mt19937 _generator(_seed);
#endif
			std::uniform_int_distribution<short> _distribution(0, 255);
			uint8_t iv[16];
			for (int i = 0; i < 16; i++)
			{
				iv[i] = (uint8_t)_distribution(_generator);
			}

			String key = ReadValue<String>(name.str() + ".token1");
			struct AES_ctx ctx;
			AES_init_ctx_iv(&ctx, (const uint8_t*)key.data(), iv);

			String _svr_encoded, _data_encoded;
			{
				AES_CBC_encrypt_buffer(&ctx, (uint8_t*)_svr_encrypt.data(), (uint32_t)_svr_encrypt.size());
				String _svr_encrypt_combined = String((const char*)iv, sizeof(iv)) + String(_svr_encrypt.begin(), _svr_encrypt.end());
				Base64Encode(_svr_encrypt_combined, _svr_encoded);
			}
			{
				memcpy(iv, ctx.Iv, sizeof(iv));
				AES_CBC_encrypt_buffer(&ctx, (uint8_t*)_data_encrypt.data(), (uint32_t)_data_encrypt.size());
				String _data_encrypt_combined = String((const char*)iv, sizeof(iv)) + String(_data_encrypt.begin(), _data_encrypt.end());
				Base64Encode(_data_encrypt_combined, _data_encoded);
			}
			Encoder _encoder;
			_svr_encoded = _encoder.UTF8UrlEncode(_svr_encoded);
			_data_encoded = _encoder.UTF8UrlEncode(_data_encoded);
			LOG_D(TAG, "svr_encoded = %s", _svr_encoded.c_str());
			LOG_D(TAG, "data_encoded = %s", _data_encoded.c_str());

			using namespace httplib;
			SSLClient cli(ReadValue<String>(name.str() + ".serviceHost"));

			const char SYS_ERROR[] = "-1";
			auto res = cli.Post("/api/init",
				(String)"store_uid=" + ReadValue<String>(name.str() + ".storeId") + "&service=" + _svr_encoded + "&encry_data=" + _data_encoded,
				"application/x-www-form-urlencoded");
			if (res) {
				std::cout << res->status << std::endl;
				std::cout << res->get_header_value("Content-Type") << std::endl;
				std::cout << res->body << std::endl;
				json _json_result = json::parse(res->body);
				String _ret_code = "";
				String _ret_msg = "";
				if (!_json_result["code"].is_null())
				{
					_ret_code = _json_result["code"].get<String>();
					_ret_msg = _json_result["msg"].get<String>();
				}
				else
				{
					_ret_code = SYS_ERROR;
					_ret_msg = _json_result["msg"].get<String>();
				}
				if (_ret_code != "B200")
				{
					CancelOrder::Error _error;
					_error.set_errcode(_ret_code);
					_error.set_message(_ret_msg);
					SendEvent(CancelOrder::Error::descriptor()->full_name(), _error.SerializeAsString());
				}
				else
				{
					CancelOrder::Result _result;
					_result.set_code(_ret_code);
					_result.set_message(_ret_msg);
					_result.set_orderid(_json_result["order_id"].get<String>());
					_result.set_groupid(_json_result["group_id"].get<String>());
					SendEvent(CancelOrder::Result::descriptor()->full_name(), _result.SerializeAsString());
				}
			}
			else
			{
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
				auto result = cli.get_openssl_verify_result();
				if (result) {
					std::cout << "verify error: " << X509_verify_cert_error_string(result) << ", key=" << key << std::endl;
				}
#endif
				CancelOrder::Error _error;
				_error.set_errcode(SYS_ERROR);
				_error.set_message((String)"OpenSSL verify error: " + X509_verify_cert_error_string(result));
				SendEvent(CancelOrder::Error::descriptor()->full_name(), _error.SerializeAsString());
			}
			break;
		}
		default:
			break;
		}
	}

	void MyPay::Base64Encode(const String& input , String& output)
	{
		const unsigned int encodedLength = cbase64_calc_encoded_length((unsigned int)input.size());
		char* codeOut = (char*)output.assign(encodedLength, '\0').data();
		char* codeOutEnd = codeOut;

		cbase64_encodestate encodeState;
		cbase64_init_encodestate(&encodeState);
		codeOutEnd += cbase64_encode_block((const unsigned char*)input.data(), (unsigned int)input.size(), codeOutEnd, &encodeState);
		codeOutEnd += cbase64_encode_blockend(codeOutEnd, &encodeState);

		unsigned int length_out = (unsigned int)(codeOutEnd - codeOut);
		if (length_out < encodedLength)
		{
			output.assign(output.begin(), output.begin() + length_out);
		}
	}

	void MyPay::CopyAndPatch(const String& in, String& out)
	{
		char _patch_code = 16 - in.size() % 16;
		size_t _size = (in.size() / 16 + 1) * 16;
		out.assign(_size, _patch_code);
		in.copy((char*)out.c_str(), in.size());
	}
}