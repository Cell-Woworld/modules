#include "ScriptAgent.h"
#include "../proto/ScriptAgent.pb.h"
#include <filesystem>
#include <fstream>

#define TAG "ScriptAgent"

namespace ScriptAgent
{

#ifndef STATIC_API
	extern "C" PUBLIC_API BioSys::RNA* CreateInstance(BioSys::IBiomolecule* owner)
	{
		return new ScriptAgent(owner);
	}
#endif

	ScriptAgent::ScriptAgent(BioSys::IBiomolecule* owner)
		:RNA(owner, "ScriptAgent", this)
	{
		init();
	}

	ScriptAgent::~ScriptAgent()
	{
	}

	void ScriptAgent::OnEvent(const DynaArray& name)
	{
		USING_BIO_NAMESPACE
		const String& _name = name.str();
		switch (hash(_name))
		{
		case "ScriptAgent.Script"_hash:
		{
			String _command = ReadValue<String>(_name + ".command");
			String _content = ReadValue<String>(_name + ".content");
			String output;
			String s = ssystem(_content);
			std::istringstream iss(s);
			while (std::getline(iss, output))
			{
				LOG_D(TAG, "ScriptAgent.Script result: %s", output.c_str());
				if (output.size() == 1 && output[0] == -1)
				{
					output = "";
				}
				Script::Result _result;
				_result.set_command(_command);
				_result.set_response(output);
				SendEvent(_result.descriptor()->full_name(), _result.SerializeAsString());
			}
			break;
		}
		default:
			break;
		}
	}

	String ScriptAgent::ssystem(const String& command) {
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
			LOG_W(TAG, "exception from ScriptAgent: %s", e.what());
		}
		return result;
	}
}