#include "NodeJS.h"
#include "../proto/NodeJS.pb.h"
#include<iostream>
#include<fstream>
#include<sstream>
#include<cstdlib>
#include <regex>
#include <filesystem>
#include "uuid.h"
#include "nlohmann/json.hpp"

#define TAG "NodeJS"

using json = nlohmann::json;

namespace NodeJS
{

#ifndef STATIC_API
	extern "C" PUBLIC_API BioSys::RNA* CreateInstance(BioSys::IBiomolecule* owner)
	{
		return new NodeJS(owner);
	}
#endif

	NodeJS::NodeJS(BioSys::IBiomolecule* owner)
		:RNA(owner, "NodeJS", this)
	{
		init();
	}

	NodeJS::~NodeJS()
	{
	}

	void NodeJS::OnEvent(const DynaArray& name)
	{
		USING_BIO_NAMESPACE
		const String& _name = name.str();
		switch (hash(_name))
		{
		case "NodeJS.Eval"_hash:
		{
			String _command = ReadValue<String>(_name + ".command");
			String _content = ReadValue<String>(_name + ".content");
			String bash = "";
			String _filename = "";
			if (_content == "")
			{
				bash = (String)"node \"" + ReadValue<String>(_name + ".file") + "\"";
			}
			else
			{
				//bash = "chcp 65001 && node -e \"" + _content + "\"";
				String _root_path = GetRootPath() + "script/";
				const String _short_filename = uuids::to_string(uuids::uuid_system_generator()());
				_filename = _root_path + _short_filename;
				std::ofstream _file(_filename.c_str());
				_file << _content;
				_file.flush();
				bash = "node \"" + _filename + "\"";
			}
			String in;
			String s = ssystem(bash);
			std::istringstream iss(s);
			String output;
			while (std::getline(iss, output))
			{
				LOG_D(TAG, "NodeJS.Eval result: %s", output.c_str());
				try
				{
					json _response;
					if (output.size() == 1 && output[0] == '\xFF')
						continue;
					if (_command == "Gmail")
					{
						if (output.substr(0, strlen("response:")) == "response:")
						{
							_response = json::parse(output.substr(strlen("response:")));
							Eval::Result _result;
							_result.set_command(_command);
							_result.set_response(output);
							SendEvent(_result.descriptor()->full_name(), _result.SerializeAsString());
						}
						else if (output.substr(0, strlen("error:")) == "error:")
						{
							_response = json::parse(output.substr(strlen("error:")));
							if (!_response.empty())
							{
								Eval::Error _error;
								_error.set_code(_response["code"].get<int>());
								_error.set_message(_response["message"].get<String>());
								SendEvent(_error.descriptor()->full_name(), _error.SerializeAsString());
							}
						}
					}
					else
					{
						_response = json::parse(output);
						if (_response["errCode"] == 0 || _response["code"] == 200)
						{
							Eval::Result _result;
							_result.set_command(_command);
							_result.set_response(output);
							SendEvent(_result.descriptor()->full_name(), _result.SerializeAsString());
						}
						else if (_response["code"].is_number_integer() && !_response["message"].empty())
						{
							Eval::Error _error;
							_error.set_code(_response["code"].get<int>());
							if (_response["message"].is_object())
							{
								_error.set_message(_response["message"].dump());
							}
							else
							{
								_error.set_message(_response["message"].get<String>());
							}
							SendEvent(_error.descriptor()->full_name(), _error.SerializeAsString());
						}
					}
				}
				catch (const json::exception&)
				{
					LOG_E(TAG, "Fail to parse NodeJS response: %s", output.c_str());
					Eval::Error _error;
					_error.set_code(-1);
					_error.set_message("Unknown response: " + output);
					SendEvent(_error.descriptor()->full_name(), _error.SerializeAsString());
				}
			}
			if (_filename != "")
			{
				remove(_filename.c_str());
			}
			break;
		}
		default:
			break;
		}
	}

	String NodeJS::ssystem(const String& command) {
		char tmpname[L_tmpnam];
		std::tmpnam(tmpname);
		String cmd = command + " >> " + tmpname;
		//std::regex_replace(cmd, std::regex("\r\n"), ""); 
		//cmd.erase(std::remove(cmd.begin(), cmd.end(), '\n'), cmd.end());
		LOG_D(TAG, "eval content: %s", cmd.c_str());
		namespace fs = std::filesystem;
		String result = "";
		try
		{
			std::system(cmd.c_str());
			std::ifstream file(tmpname, std::ios::in);
			if (file) {
				while (!file.eof()) result.push_back(file.get());
				file.close();
			}
			remove(tmpname);
		}
		catch (const std::exception& e)
		{
			LOG_W(TAG, "exception from node: %s", e.what());
		}
		return result;
	}
}