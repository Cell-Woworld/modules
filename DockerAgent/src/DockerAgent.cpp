#include "DockerAgent.h"
#include "../proto/DockerAgent.pb.h"
#include <filesystem>
#include <fstream>

#define TAG "DockerAgent"

namespace DockerAgent
{

#ifndef STATIC_API
	extern "C" PUBLIC_API BioSys::RNA* CreateInstance(BioSys::IBiomolecule* owner)
	{
		return new DockerAgent(owner);
	}
#endif

	DockerAgent::DockerAgent(BioSys::IBiomolecule* owner)
		:RNA(owner, "DockerAgent", this)
	{
		init();
	}

	DockerAgent::~DockerAgent()
	{
	}

	void DockerAgent::OnEvent(const DynaArray& name)
	{
		USING_BIO_NAMESPACE
		const String& _name = name.str();
		switch (hash(_name))
		{
		case "DockerAgent.Docker"_hash:
		{
			String _command = ReadValue<String>(_name + ".command");
			String _params = ReadValue<String>(_name + ".params");
			String output;
#ifdef _WIN32
			output = "response from DockerAgent.Docker of Windows version";
			Docker::Result _result;
			_result.set_command(_command);
			_result.set_response(output);
			SendEvent(_result.descriptor()->full_name(), _result.SerializeAsString());
#else
			String s = ssystem((String)"docker " + _command + " " + _params);
			std::istringstream iss(s);
			while (std::getline(iss, output))
			{
				LOG_D(TAG, "DockerAgent.Docker result: %s", output.c_str());
				if (output.size() == 1 && output[0] == -1)
				{
					output = "";
				}
				Docker::Result _result;
				_result.set_command(_command);
				_result.set_response(output);
				SendEvent(_result.descriptor()->full_name(), _result.SerializeAsString());
			}
#endif
			break;
		}
		case "DockerAgent.Compose"_hash:
		{
			String _filename = ReadValue<String>(_name + ".config_filename");
			String _command = ReadValue<String>(_name + ".command");
			String _params = ReadValue<String>(_name + ".params");
			String output;
#ifdef _WIN32
			output = "response from DockerAgent.Compose of Windows version";
			Compose::Result _result;
			_result.set_command(_command);
			_result.set_response(output);
			SendEvent(_result.descriptor()->full_name(), _result.SerializeAsString());
#else
			String _full_path = GetRootPath() + _filename;
			String s = ssystem((String)"docker-compose -f \"" + _full_path + "\" " + _command + " " + _params);
			std::istringstream iss(s);
			while (std::getline(iss, output))
			{
				LOG_D(TAG, "DockerAgent.Compose result: %s", output.c_str());
				if (output.size() == 1 && output[0] == -1)
				{
					output = "";
				}
				Compose::Result _result;
				_result.set_command(_command);
				_result.set_response(output);
				SendEvent(_result.descriptor()->full_name(), _result.SerializeAsString());
			}
#endif
			break;
		}
		default:
			break;
		}
	}

	String DockerAgent::ssystem(const String& command) {
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
			LOG_W(TAG, "exception from DockerAgent: %s", e.what());
		}
		return result;
	}
}