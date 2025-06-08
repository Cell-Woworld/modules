#include "Converter.h"
#include "../proto/Converter.pb.h"
#include "UrlEncoder/Encoder.hpp"
#include "PicoSHA2/picosha2.h"
#include "hmac_sha256/hmac_sha256.h"
#include "tinyAES/aes.hpp"
#include "cbase64/include/cbase64/cbase64.h"
#include <random>
#include <iomanip>

#define TAG "Converter"

namespace Converter
{

#ifndef STATIC_API
	extern "C" PUBLIC_API BioSys::RNA* CreateInstance(BioSys::IBiomolecule* owner)
	{
		return new Converter(owner);
	}
#endif

	Converter::Converter(BioSys::IBiomolecule* owner)
		:RNA(owner, "Converter", this)
	{
		init();
	}

	Converter::~Converter()
	{
	}

	void Converter::OnEvent(const DynaArray& name)
	{
		USING_BIO_NAMESPACE
		const String& _name = name.str();
		switch (BioSys::hash(_name))
		{
		case "Converter.Push"_hash:
		{
			Array<String> _items = ReadValue<Array<String>>(_name + ".model_list");
			int _type = ReadValue<int>(_name + ".type");
			if (_type == CONVERTER_TYPE::INVALID)
			{
				String _type_name = ReadValue<String>(_name + ".type");
				const google::protobuf::EnumDescriptor* descriptor = CONVERTER_TYPE_descriptor();
				_type = descriptor->FindValueByName(_type_name)->number();
			}
			if (_type == CONVERTER_TYPE::INVALID)
				_type = ReadValue<int>(_name + ".type.value");
			switch (_type)
			{
			case AES_CBC:
			{
				String _seed(AES_BLOCKLEN, '0');
				String _seed_model = ReadValue<String>(name.str() + ".seed_model");
				if (_seed_model != "")
					_seed = ReadValue<String>(_seed_model);
				if (_seed == "")
				{
					typedef std::chrono::high_resolution_clock hrclock;
	#if defined(_M_X64) || defined(__amd64__)
					unsigned long long _rnd_seed = hrclock::now().time_since_epoch().count();
					std::mt19937_64 _generator(_rnd_seed);
	#else
					unsigned int _seed = (unsigned int)hrclock::now().time_since_epoch().count();
					std::mt19937 _generator(_seed);
	#endif
					std::uniform_int_distribution<short> _distribution(0, 255);
					for (int i = 0; i < AES_BLOCKLEN; i++)
					{
						_seed[i] = (uint8_t)_distribution(_generator);
					}
					if (_seed_model != "")
						WriteValue(_seed_model, _seed);
				}

				String _key = ReadValue<String>(name.str() + ".key");
				if (_key == "")
				{
					String _key_model = ReadValue<String>(name.str() + ".key_model");
					if (_key_model != "")
						_key = ReadValue<String>(_key_model);
				}
				assert(_key != "");
				struct AES_ctx ctx;
				AES_init_ctx_iv(&ctx, (const uint8_t*)_key.data(), (const uint8_t*)_seed.data());

				for (int i = 0; i < _items.size(); i++)
				{
					Item _item;
					nlohmann::json _root;
					String _src_name, _target_name;
					if (_item.ParseFromString(_items[i]) == true)
					{
						_src_name = _item.src();
						_target_name = _item.target();
					}
					else if ((_root = nlohmann::json::parse(_items[i])).is_object())
					{
						_src_name = _root["src"].get<String>();
						_target_name = _root["target"].get<String>();
					}
					if (_src_name != "" && _target_name != "")
					{
						String _src = ReadValue<String>(_src_name);
						String _target;
						Padding(_src, _target);
						AES_CBC_encrypt_buffer(&ctx, (uint8_t*)_target.data(), (uint32_t)_target.size());
						WriteValue(_target_name, _target);
					}
				}
				break;
			}
			case AES_CTR:
			{
				typedef std::chrono::high_resolution_clock hrclock;
#if defined(_M_X64) || defined(__amd64__)
				unsigned long long _rnd_seed = hrclock::now().time_since_epoch().count();
				std::mt19937_64 _generator(_rnd_seed);
#else
				unsigned int _seed = (unsigned int)hrclock::now().time_since_epoch().count();
				std::mt19937 _generator(_seed);
#endif
				std::uniform_int_distribution<short> _distribution(0, 255);
				String _seed(AES_BLOCKLEN, '0');
				for (int i = 0; i < AES_BLOCKLEN; i++)
				{
					_seed[i] = (uint8_t)_distribution(_generator);
				}

				String _seed_model = ReadValue<String>(name.str() + ".seed_model");
				if (_seed_model != "")
					WriteValue(_seed_model, _seed);

				String _key = ReadValue<String>(name.str() + ".key");
				struct AES_ctx ctx;
				AES_init_ctx_iv(&ctx, (const uint8_t*)_key.data(), (const uint8_t*)_seed.data());

				for (int i = 0; i < _items.size(); i++)
				{
					Item _item;
					nlohmann::json _root;
					String _src_name, _target_name;
					if (_item.ParseFromString(_items[i]) == true)
					{
						_src_name = _item.src();
						_target_name = _item.target();
					}
					else if ((_root = nlohmann::json::parse(_items[i])).is_object())
					{
						_src_name = _root["src"].get<String>();
						_target_name = _root["target"].get<String>();
					}
					if (_src_name != "" && _target_name != "")
					{
						String _src = ReadValue<String>(_src_name);
						String _target;
						Padding(_src, _target);
						AES_CTR_xcrypt_buffer(&ctx, (uint8_t*)_target.data(), (uint32_t)_target.size());
						WriteValue(_target_name, _target);
					}
				}
				break;
			}
			case AES_ECB:
			{
				String _key = ReadValue<String>(name.str() + ".key");
				struct AES_ctx ctx;
				AES_init_ctx(&ctx, (const uint8_t*)_key.data());

				for (int i = 0; i < _items.size(); i++)
				{
					Item _item;
					nlohmann::json _root;
					String _src_name, _target_name;
					if (_item.ParseFromString(_items[i]) == true)
					{
						_src_name = _item.src();
						_target_name = _item.target();
					}
					else if ((_root = nlohmann::json::parse(_items[i])).is_object())
					{
						_src_name = _root["src"].get<String>();
						_target_name = _root["target"].get<String>();
					}
					if (_src_name != "" && _target_name != "")
					{
						String _src = ReadValue<String>(_src_name);
						String _target;
						Padding(_src, _target);
						for (int j = 0; j < _target.size(); j += AES_BLOCKLEN)
						{
							AES_ECB_encrypt(&ctx, (uint8_t*)_target.data() + j * AES_BLOCKLEN);
						}
						WriteValue(_target_name, _target);
					}
				}
				break;
			}
			case SHA256:
			{
				for (int i = 0; i < _items.size(); i++)
				{
					Item _item;
					nlohmann::json _root;
					String _src_name, _target_name;
					if (_item.ParseFromString(_items[i]) == true)
					{
						_src_name = _item.src();
						_target_name = _item.target();
					}
					else if ((_root = nlohmann::json::parse(_items[i])).is_object())
					{
						_src_name = _root["src"].get<String>();
						_target_name = _root["target"].get<String>();
					}
					if (_src_name != "" && _target_name != "")
					{
						String _src = ReadValue<String>(_src_name);
						std::vector<unsigned char> _hash(picosha2::k_digest_size);
						picosha2::hash256(_src.begin(), _src.end(), _hash.begin(), _hash.end());
						WriteValue(_target_name, String(_hash.begin(), _hash.end()));
					}
				}
				break;
			}
			case HMAC_SHA256:
			{
				String _key = ReadValue<String>(name.str() + ".key");
				for (int i = 0; i < _items.size(); i++)
				{
					Item _item;
					nlohmann::json _root;
					String _src_name, _target_name;
					if (_item.ParseFromString(_items[i]) == true)
					{
						_src_name = _item.src();
						_target_name = _item.target();
					}
					else if ((_root = nlohmann::json::parse(_items[i])).is_object())
					{
						_src_name = _root["src"].get<String>();
						_target_name = _root["target"].get<String>();
					}
					if (_src_name != "" && _target_name != "")
					{
						String _src = ReadValue<String>(_src_name);
						std::vector<unsigned char> _hash(SHA256_HASH_SIZE, '\0');
						HMAC_SHA256::hmac_sha256(_key.c_str(), _key.size(), _src.c_str(), _src.size(), _hash.data(), _hash.size());
						WriteValue(_target_name, String(_hash.begin(), _hash.end()));
					}
				}
				break;
			}
			case BASE64:
				for (int i = 0; i < _items.size(); i++)
				{
					Item _item;
					nlohmann::json _root;
					String _src_name, _target_name;
					if (_item.ParseFromString(_items[i]) == true)
					{
						_src_name = _item.src();
						_target_name = _item.target();
					}
					else if ((_root = nlohmann::json::parse(_items[i])).is_object())
					{
						_src_name = _root["src"].get<String>();
						_target_name = _root["target"].get<String>();
					}
					if (_src_name != "" && _target_name != "")
					{
						String _src = ReadValue<String>(_src_name);
						String _output;
						Base64Encode(_src, _output);
						WriteValue(_target_name, _output);
					}
				}
				break;
			case URLCODER:
			{
				Encoder _encoder;
				for (int i = 0; i < _items.size(); i++)
				{
					Item _item;
					nlohmann::json _root;
					String _src_name, _target_name;
					if (_item.ParseFromString(_items[i]) == true)
					{
						_src_name = _item.src();
						_target_name = _item.target();
					}
					else if ((_root = nlohmann::json::parse(_items[i])).is_object())
					{
						_src_name = _root["src"].get<String>();
						_target_name = _root["target"].get<String>();
					}
					if (_src_name != "" && _target_name != "")
					{
						String _src = ReadValue<String>(_src_name);
						WriteValue(_target_name, _encoder.UrlEncode(_src));
					}
				}
				break;
			}
			case DOTNET_URLCODER:
			{
				Encoder _encoder;
				for (int i = 0; i < _items.size(); i++)
				{
					Item _item;
					nlohmann::json _root;
					String _src_name, _target_name;
					if (_item.ParseFromString(_items[i]) == true)
					{
						_src_name = _item.src();
						_target_name = _item.target();
					}
					else if ((_root = nlohmann::json::parse(_items[i])).is_object())
					{
						_src_name = _root["src"].get<String>();
						_target_name = _root["target"].get<String>();
					}
					if (_src_name != "" && _target_name != "")
					{
						String _src = ReadValue<String>(_src_name);
						WriteValue(_target_name, _encoder.dotNetUrlEncode(_src));
					}
				}
				break;
			}
			case ASCII_HEX:
				for (int i = 0; i < _items.size(); i++)
				{
					Item _item;
					nlohmann::json _root;
					String _src_name, _target_name;
					if (_item.ParseFromString(_items[i]) == true)
					{
						_src_name = _item.src();
						_target_name = _item.target();
					}
					else if ((_root = nlohmann::json::parse(_items[i])).is_object())
					{
						_src_name = _root["src"].get<String>();
						_target_name = _root["target"].get<String>();
					}
					if (_src_name != "" && _target_name != "")
					{
						String _src = ReadValue<String>(_src_name);
						String _target;
						ASCIIToHex(_src, _target);
						WriteValue(_target_name, _target);
					}
				}
				break;
			case JSON_ESCAPE:
				for (int i = 0; i < _items.size(); i++)
				{
					Item _item;
					nlohmann::json _root;
					String _src_name, _target_name;
					if (_item.ParseFromString(_items[i]) == true)
					{
						_src_name = _item.src();
						_target_name = _item.target();
					}
					else if ((_root = nlohmann::json::parse(_items[i])).is_object())
					{
						_src_name = _root["src"].get<String>();
						_target_name = _root["target"].get<String>();
					}
					if (_src_name != "" && _target_name != "")
					{
						String _src = ReadValue<String>(_src_name);
						String _target;
						EscapeJSON(_src, _target);
						WriteValue(_target_name, _target);
					}
				}
				break;
			case DECIMAL_BASE62:
				for (int i = 0; i < _items.size(); i++)
				{
					Item _item;
					nlohmann::json _root;
					String _src_name, _target_name;
					if (_item.ParseFromString(_items[i]) == true)
					{
						_src_name = _item.src();
						_target_name = _item.target();
					}
					else if ((_root = nlohmann::json::parse(_items[i])).is_object())
					{
						_src_name = _root["src"].get<String>();
						_target_name = _root["target"].get<String>();
					}
					if (_src_name != "" && _target_name != "")
					{
						unsigned long long _src = ReadValue<unsigned long long>(_src_name);
						String _target;
						_target = toBase62(_src);
						WriteValue(_target_name, _target);
					}
				}
				break;
			default:
				break;
			}
			break;
		}
		case "Converter.Pull"_hash:
		{
			Array<String> _items = ReadValue<Array<String>>(_name + ".model_list");
			int _type = ReadValue<int>(_name + ".type");
			if (_type == CONVERTER_TYPE::INVALID)
			{
				String _type_name = ReadValue<String>(_name + ".type");
				const google::protobuf::EnumDescriptor* descriptor = CONVERTER_TYPE_descriptor();
				_type = descriptor->FindValueByName(_type_name)->number();
			}
			if (_type == CONVERTER_TYPE::INVALID)
				_type = ReadValue<int>(_name + ".type.value");
			switch (_type)
			{
			case AES_CBC:
			{
				String _seed = ReadValue<String>(name.str() + ".seed");
				assert(_seed.size() == AES_BLOCKLEN);
				String _key = ReadValue<String>(name.str() + ".key");
				struct AES_ctx ctx;
				AES_init_ctx_iv(&ctx, (const uint8_t*)_key.data(), (const uint8_t*)_seed.data());

				for (int i = 0; i < _items.size(); i++)
				{
					Item _item;
					nlohmann::json _root;
					String _src_name, _target_name;
					if (_item.ParseFromString(_items[i]) == true)
					{
						_src_name = _item.src();
						_target_name = _item.target();
					}
					else if ((_root = nlohmann::json::parse(_items[i])).is_object())
					{
						_src_name = _root["src"].get<String>();
						_target_name = _root["target"].get<String>();
					}
					if (_src_name != "" && _target_name != "")
					{
						String _src = ReadValue<String>(_src_name);
						AES_CBC_decrypt_buffer(&ctx, (uint8_t*)_src.data(), (uint32_t)_src.size());
						String _target;
						Unpadding(_src, _target);
						WriteValue(_target_name, _target);
					}
				}
				break;
			}
			case AES_CTR:
			{
				String _seed = ReadValue<String>(name.str() + ".seed");
				assert(_seed.size() == AES_BLOCKLEN);
				String _key = ReadValue<String>(name.str() + ".key");
				struct AES_ctx ctx;
				AES_init_ctx_iv(&ctx, (const uint8_t*)_key.data(), (const uint8_t*)_seed.data());

				for (int i = 0; i < _items.size(); i++)
				{
					Item _item;
					nlohmann::json _root;
					String _src_name, _target_name;
					if (_item.ParseFromString(_items[i]) == true)
					{
						_src_name = _item.src();
						_target_name = _item.target();
					}
					else if ((_root = nlohmann::json::parse(_items[i])).is_object())
					{
						_src_name = _root["src"].get<String>();
						_target_name = _root["target"].get<String>();
					}
					if (_src_name != "" && _target_name != "")
					{
						String _src = ReadValue<String>(_src_name);
						AES_CTR_xcrypt_buffer(&ctx, (uint8_t*)_src.data(), (uint32_t)_src.size());
						String _target;
						Unpadding(_src, _target);
						WriteValue(_target_name, _target);
					}
				}
				break;
			}
			break;
			case AES_ECB:
			{
				String _key = ReadValue<String>(name.str() + ".key");
				struct AES_ctx ctx;
				AES_init_ctx(&ctx, (const uint8_t*)_key.data());

				for (int i = 0; i < _items.size(); i++)
				{
					Item _item;
					nlohmann::json _root;
					String _src_name, _target_name;
					if (_item.ParseFromString(_items[i]) == true)
					{
						_src_name = _item.src();
						_target_name = _item.target();
					}
					else if ((_root = nlohmann::json::parse(_items[i])).is_object())
					{
						_src_name = _root["src"].get<String>();
						_target_name = _root["target"].get<String>();
					}
					if (_src_name != "" && _target_name != "")
					{
						String _src = ReadValue<String>(_src_name);
						for (int j = 0; j < _src.size(); j += AES_BLOCKLEN)
						{
							AES_ECB_decrypt(&ctx, (uint8_t*)_src.data() + j * AES_BLOCKLEN);
						}
						String _target;
						Unpadding(_src, _target);
						WriteValue(_target_name, _target);
					}
				}
				break;
			}
			case SHA256:
				LOG_E(TAG, "Unsupported function, please use Converter.Push to hash");
				break;
			case BASE64:
				for (int i = 0; i < _items.size(); i++)
				{
					Item _item;
					nlohmann::json _root;
					String _src_name, _target_name;
					if (_item.ParseFromString(_items[i]) == true)
					{
						_src_name = _item.src();
						_target_name = _item.target();
					}
					else if ((_root = nlohmann::json::parse(_items[i])).is_object())
					{
						_src_name = _root["src"].get<String>();
						_target_name = _root["target"].get<String>();
					}
					if (_src_name != "" && _target_name != "")
					{
						String _src = ReadValue<String>(_src_name);
						String _output;
						Base64Decode(_src, _output);
						WriteValue(_target_name, _output);
					}
				}
				break;
			case URLCODER:
			{
				Encoder _encoder;
				for (int i = 0; i < _items.size(); i++)
				{
					Item _item;
					nlohmann::json _root;
					String _src_name, _target_name;
					if (_item.ParseFromString(_items[i]) == true)
					{
						_src_name = _item.src();
						_target_name = _item.target();
					}
					else if ((_root = nlohmann::json::parse(_items[i])).is_object())
					{
						_src_name = _root["src"].get<String>();
						_target_name = _root["target"].get<String>();
					}
					if (_src_name != "" && _target_name != "")
					{
						String _src = ReadValue<String>(_src_name);
						WriteValue(_target_name, _encoder.UrlDecode(_src));
					}
				}
				break;
			}
			case DOTNET_URLCODER:
			{
				Encoder _encoder;
				for (int i = 0; i < _items.size(); i++)
				{
					Item _item;
					nlohmann::json _root;
					String _src_name, _target_name;
					if (_item.ParseFromString(_items[i]) == true)
					{
						_src_name = _item.src();
						_target_name = _item.target();
					}
					else if ((_root = nlohmann::json::parse(_items[i])).is_object())
					{
						_src_name = _root["src"].get<String>();
						_target_name = _root["target"].get<String>();
					}
					if (_src_name != "" && _target_name != "")
					{
						String _src = ReadValue<String>(_src_name);
						WriteValue(_target_name, _encoder.dotNetUrlDecode(_src));
					}
				}
				break;
			}
			case ASCII_HEX:
				for (int i = 0; i < _items.size(); i++)
				{
					Item _item;
					nlohmann::json _root;
					String _src_name, _target_name;
					if (_item.ParseFromString(_items[i]) == true)
					{
						_src_name = _item.src();
						_target_name = _item.target();
					}
					else if ((_root = nlohmann::json::parse(_items[i])).is_object())
					{
						_src_name = _root["src"].get<String>();
						_target_name = _root["target"].get<String>();
					}
					if (_src_name != "" && _target_name != "")
					{
						String _src = ReadValue<String>(_src_name);
						String _target;
						HexToASCII(_src, _target);
						WriteValue(_target_name, _target);
					}
				}
				break;
			case DECIMAL_BASE62:
				for (int i = 0; i < _items.size(); i++)
				{
					Item _item;
					nlohmann::json _root;
					String _src_name, _target_name;
					if (_item.ParseFromString(_items[i]) == true)
					{
						_src_name = _item.src();
						_target_name = _item.target();
					}
					else if ((_root = nlohmann::json::parse(_items[i])).is_object())
					{
						_src_name = _root["src"].get<String>();
						_target_name = _root["target"].get<String>();
					}
					if (_src_name != "" && _target_name != "")
					{
						String _src = ReadValue<String>(_src_name);
						unsigned long long  _target;
						_target = toBase10(_src);
						WriteValue(_target_name, _target);
					}
				}
				break;
			default:
				break;
			}
		}
		default:
			break;
		}
	}

	void Converter::Base64Encode(const String& input, String& output)
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

	void Converter::Base64Decode(const String& input, String& output)
	{
		const unsigned int decodedLength = cbase64_calc_decoded_length(input.data(), (unsigned int)input.size());
		unsigned char* dataOut = (unsigned char*)output.assign(decodedLength, '\0').data();

		cbase64_decodestate decodeState;
		cbase64_init_decodestate(&decodeState);
		unsigned int length_out = cbase64_decode_block(input.data(), (unsigned int)input.size(), dataOut, &decodeState);
		if (length_out < decodedLength)
		{
			output.assign(output.begin(), output.begin() + length_out);
		}
	}

	void Converter::Padding(const String& in, String& out)
	{
		char _patch_code = AES_BLOCKLEN - in.size() % AES_BLOCKLEN;
		size_t _size = 0;
		if (_patch_code == AES_BLOCKLEN)
			_size = in.size();
		else
			_size = (in.size() / AES_BLOCKLEN + 1) * AES_BLOCKLEN;
		out.assign(_size, _patch_code);
		in.copy((char*)out.c_str(), in.size());
	}

	void Converter::Unpadding(const String& in, String& out)
	{
		char _patch_code = in.back();
		size_t _size = in.size() - _patch_code;
		out.assign(_size, '\0');
		in.copy((char*)out.c_str(), in.size());
	}

	void Converter::ASCIIToHex(const String& in, String& out)
	{
		static const char hex_digits[] = "0123456789ABCDEF";

		out.reserve(in.length() * 2);
		for (unsigned char c : in)
		{
			out.push_back(hex_digits[c >> 4]);
			out.push_back(hex_digits[c & 15]);
		}
	}

	void Converter::HexToASCII(const String& in, String& out)
	{
		// C++98 guarantees that '0', '1', ... '9' are consecutive.
		// It only guarantees that 'a' ... 'f' and 'A' ... 'F' are
		// in increasing order, but the only two alternative encodings
		// of the basic source character set that are still used by
		// anyone today (ASCII and EBCDIC) make them consecutive.
		auto hexval = [](unsigned char c)
		{
			if ('0' <= c && c <= '9')
				return c - '0';
			else if ('a' <= c && c <= 'f')
				return c - 'a' + 10;
			else if ('A' <= c && c <= 'F')
				return c - 'A' + 10;
			else throw std::invalid_argument("invalid hex digit");
		};

		out.clear();
		out.reserve(in.length() / 2);
		for (string::const_iterator p = in.begin(); p != in.end(); p++)
		{
			unsigned char c = hexval(*p);
			p++;
			if (p == in.end()) break; // incomplete last digit - should report error
			c = (c << 4) + hexval(*p); // + takes precedence over <<
			out.push_back(c);
		}
	}

	void Converter::EscapeJSON(const String& in, String& out)
	{
		std::ostringstream o;
		for (auto c = in.cbegin(); c != in.cend(); c++) {
			switch (*c) {
			case '"': o << "\\\""; break;
			case '\\': o << "\\\\"; break;
			case '\b': o << "\\b"; break;
			case '\f': o << "\\f"; break;
			case '\n': o << "\\n"; break;
			case '\r': o << "\\r"; break;
			case '\t': o << "\\t"; break;
			default:
				if ('\x00' <= *c && *c <= '\x1f') {
					o << "\\u"
						<< std::hex << std::setw(4) << std::setfill('0') << (int)*c;
				}
				else {
					o << *c;
				}
			}
		}
		out = o.str();
	}

	String Converter::toBase62(unsigned long long value) {
		const String CODES = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
		String str;
		do {
			str.insert(0, string(1, CODES[value % 62]));
			value /= 62;
		} while (value > 0);

		return str;
	}

	unsigned long long Converter::toBase10(String value) {
		const String CODES = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
		std::reverse(value.begin(), value.end());

		unsigned long long ret = 0;
		unsigned long long count = 1;
		for (const char& character : value) {
			ret += CODES.find(character) * count;
			count *= 62;
		}

		return ret;
	}
}


