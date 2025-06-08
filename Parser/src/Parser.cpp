#include "Parser.h"
#include "../proto/Parser.pb.h"
#include <regex>

#define TAG "Parser"

namespace Parser
{

#ifndef STATIC_API
	extern "C" PUBLIC_API BioSys::RNA* CreateInstance(BioSys::IBiomolecule* owner)
	{
		return new Parser(owner);
	}
#endif

	Parser::Parser(BioSys::IBiomolecule* owner)
		:RNA(owner, "Parser", this)
	{
		init();
	}

	Parser::~Parser()
	{
	}

	void Parser::OnEvent(const DynaArray& name)
	{
		USING_BIO_NAMESPACE
		const String& _name = name.str();
		switch (hash(_name))
		{
		case "Parser.ParseCSV"_hash:
		{
			String _data = ReadValue<String>(_name + ".data");
			if (_data == "")
			{
				String _filename = ReadValue<String>(_name + ".filename");
				try
				{
					FILE* _file = fopen((GetRootPath() + _filename).c_str(), "rb");
					if (_file == nullptr)
						_file = fopen(_filename.c_str(), "rb");
					if (_file != nullptr)
					{
						fseek(_file, 0, SEEK_END);
						size_t _size = ftell(_file);
						fseek(_file, 0, SEEK_SET);
						_data = String(_size, 0);
						_size = fread((void*)_data.data(), sizeof(unsigned char), _size, _file);
						fclose(_file);
					}
				}
				catch (const std::exception & e)
				{
					LOG_W(TAG, "Parser.ParseCSV() exception (%s) thrown by fopen(%s)", e.what(), _filename.c_str());
					return;
				}
			}
			LOG_D(TAG, "Parser.ParseCSV() input: %s", _data.c_str());
			_data = std::regex_replace(_data, std::regex("\r\n"), "\n");
			_data = std::regex_replace(_data, std::regex("\r"), "\n");
			String _delimeter = ReadValue<String>(_name + ".delimeter");
			if (_delimeter == "")
				_delimeter = ",";
			Array<String> _recordset;
			Split(_recordset, _data, "\n");
			for (int i = 1; i < _recordset.size(); i++)		// ignoring 0 because it is header not data
			{
				Array<String> _output;
				Split(_output, _recordset[i], _delimeter);
				WriteValue(_name + ".Result.output", _output);
				SendEvent(_name + ".Result");
			}
			SendEvent(_name + ".Done");
			break;
		}
		case "Parser.ExportCSV"_hash:
		{
			String _content = ReadValue<String>(_name + ".data");
			if (_content == "")
				break;
			_content = std::regex_replace(_content, std::regex("\\],\\["), "\n");
			_content.erase(std::remove(_content.begin(), _content.end(), '['), _content.end());
			_content.erase(std::remove(_content.begin(), _content.end(), ']'), _content.end());
			{
				String _filename = ReadValue<String>(_name + ".filename");
				try
				{
					FILE* _file = fopen((GetRootPath() + _filename).c_str(), "wb");
					if (_file == nullptr)
						_file = fopen(_filename.c_str(), "wb");
					if (_file != nullptr)
					{
						size_t _size = fwrite((void*)_content.data(), sizeof(unsigned char), _content.size(), _file);
						fclose(_file);
					}
				}
				catch (const std::exception& e)
				{
					LOG_W(TAG, "Parser.ExportCSV() exception (%s) thrown by fopen/fwrite(%s)", e.what(), _filename.c_str());
					return;
				}
			}
			SendEvent(_name + ".Done");
			break;
		}
		default:
			LOG_E(TAG, "Unsupported action: %s", _name.c_str());
			break;
		}
	}

	void Parser::Split(Array<String>& output, const String& input, const String& separator)
	{
		size_t found;
		String str = input;
		found = str.find_first_of(separator);
		while (found != String::npos)
		{
			output.push_back(str.substr(0, found));
			str = str.substr(found + 1);
			found = str.find_first_not_of("\n ");
			if (found != String::npos)
				str = str.substr(found);
			found = str.find_first_of(separator);
		}
		if (str.length() > 0)
		{
			output.push_back(str);
		}
	}
}