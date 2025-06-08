#include "PythonAgent.h"
#include "../proto/PythonAgent.pb.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <regex>
#include <filesystem>
#include "nlohmann/json.hpp"
#include <Python.h>
#include "uuid.h"


#define TAG "PythonAgent"

using json = nlohmann::json;

namespace PythonAgent
{

#ifndef STATIC_API
	extern "C" PUBLIC_API BioSys::RNA* CreateInstance(BioSys::IBiomolecule* owner)
	{
		return new PythonAgent(owner);
	}
#endif
	Mutex PythonAgent::mutexPython_;

	PythonAgent::PythonAgent(BioSys::IBiomolecule* owner)
		:RNA(owner, "PythonAgent", this)
	{
		init();
	}

	PythonAgent::~PythonAgent()
	{
	}

	void PythonAgent::OnEvent(const DynaArray& name)
	{
		USING_BIO_NAMESPACE
		const String& _name = name.str();
		switch (hash(_name))
		{
		case "PythonAgent.Eval"_hash:
		{
			MutexLocker _lock(mutexPython_);
			Py_Initialize();
			String _command_group = ReadValue<String>(_name + ".command_group");
			String _command = ReadValue<String>(_name + ".command");
			String _filename = ReadValue<String>(_name + ".file");
			Array<String> _params = ReadValue<Array<String>>(_name + ".params");
			String _extra_params = ReadValue<String>(_name + ".extra_params");
			PyObject* pModule = NULL;
			PyObject* pFunc = NULL;

			String _full_path = GetRootPath() + _filename;
			String _folder;
			size_t _pos = _full_path.find_last_of("/\\");
			if (_pos != String::npos)
			{
				_filename = _full_path.substr(_pos + 1);
				_folder = _full_path.substr(0, _pos + 1);
				PyRun_SimpleString("import sys");
				//PyRun_SimpleString(("sys.path.append('" + _folder + "')").c_str());
				PyRun_SimpleString(("sys.path.insert(0, '" + _folder + "')").c_str());		// for 3rd-party okex v5 API. It must be prior to site-packages
#ifdef _DEBUG
				PyRun_SimpleString("print('current sys.path', sys.path)");
#endif
			}
			_pos = _filename.find(".py");
			if (_pos != String::npos)
			{
				_filename = _filename.substr(0, _pos);
			}
			pModule = PyImport_ImportModule(_filename.c_str());
			if (!pModule) {
				//LOG_E(TAG, "Invalid module %s in file %s", _filename.c_str(), _full_path.c_str());
				PyErr_Print();
				Py_Finalize();
				return;
			}
			pFunc = PyObject_GetAttrString(pModule, _command_group.c_str());
			// if (pFunc == NULL)
			if (!pFunc || !PyCallable_Check(pFunc)) {
				//LOG_E(TAG, "No such function %s in module %s", _command.c_str(), _filename.c_str());
				if (PyErr_Occurred())
					PyErr_Print();
				Py_CLEAR(pModule);
				Py_CLEAR(pFunc);
				Py_Finalize();
				return;
			}
			PyObject* pArgs = PyTuple_New(_params.size() + 1 + (_extra_params != "" ? 1 : 0));
			for (int i = 0; i < _params.size(); i++)
			{
				PyTuple_SetItem(pArgs, i, Py_BuildValue("s", _params[i].c_str()));
			}
			PyTuple_SetItem(pArgs, _params.size(), Py_BuildValue("s", _command.c_str()));
			if (_extra_params != "")
			{
				PyTuple_SetItem(pArgs, _params.size() + 1, Py_BuildValue("s", _extra_params.c_str()));
			}
			PyObject* pReturn = NULL;
			pReturn = PyObject_CallObject(pFunc, pArgs);
			if (pReturn)
			{
				if (strcmp(pReturn->ob_type->tp_name, "NoneType") != 0)
				{
					if (strcmp(pReturn->ob_type->tp_name, "str") == 0)
					{
						char* _retval = nullptr;
						PyArg_Parse(pReturn, "s", &_retval);
						if (_retval != nullptr)
						{
							Eval::Result _result;
							_result.set_command(_command);
							_result.set_response(_retval);
							SendEvent(_result.descriptor()->full_name(), _result.SerializeAsString());
						}
					}
					else
					{
						LOG_I(TAG, "Unhandled type=%s", pReturn->ob_type->tp_name);
						PyObject_Print(pReturn, stdout, Py_PRINT_RAW);
					}
				}
			}
			else
			{
				Eval::Error _error;
				_error.set_code(-1);
				_error.set_message("API connection error! Maybe server's system time is incorrect!");
				SendEvent(_error.descriptor()->full_name(), _error.SerializeAsString());
			}
			Py_CLEAR(pModule);
			Py_CLEAR(pFunc);
			Py_CLEAR(pArgs);
			Py_CLEAR(pReturn);
			Py_Finalize();
			break;
		}
		case "PythonAgent.EvalViaShell"_hash:
		{
			String _command = ReadValue<String>(_name + ".command");
			String _filename = ReadValue<String>(_name + ".file");
			Array<String> _params = ReadValue<Array<String>>(_name + ".params");
			String _extra_params = ReadValue<String>(_name + ".extra_params");
			if (_extra_params == "")
			{
				WriteValue(_name + ".extra_params", "{}");
			}
			String bash = "";

			String _buf;
			try
			{
				MutexLocker _lock(mutexPython_);
				FILE* _file = fopen((GetRootPath() + _filename).c_str(), "rb");
				if (_file == nullptr)
				{
					_file = fopen(_filename.c_str(), "rb");
				}
				if (_file != nullptr)
				{
					fseek(_file, 0, SEEK_END);
					size_t _size = ftell(_file);
					fseek(_file, 0, SEEK_SET);
					_buf.assign(_size, 0);
					_size = fread((void*)_buf.data(), sizeof(unsigned char), _size, _file);
					fclose(_file);
				}
				else
				{
					LOG_W(TAG, "fail to open file \"%s\"", _filename.c_str());
					break;
				}
			}
			catch (const std::exception& e)
			{
				LOG_W(TAG, "exception (%s) thrown by fopen/fread(%s)", e.what(), _filename.c_str());
			}

			ReplaceModelNameWithValue(_buf);

			//bash = "chcp 65001 && python -e \"" + _buf + "\"";
			String _root_path = GetRootPath() + "script/";
			const String _short_filename = uuids::to_string(uuids::uuid_system_generator()());
			String _script_filename = _root_path + _short_filename + ".py";
			std::ofstream _file(_script_filename.c_str());
			_file << _buf;
			_file.flush();
			_file.close();
			bash = "python \"" + _script_filename + "\"";

			String in;
			String s = ssystem(bash);
			std::istringstream iss(s);
			String output;
			bool _null_result = true;
			while (std::getline(iss, output))
			{
				LOG_D(TAG, "PythonAgent.EvalViaShell result: %s", output.c_str());
				try
				{
					if (output.size() == 1 && output[0] == '\xFF')
					{
						if (_null_result)
						{
							EvalViaShell::Error _error;
							_error.set_code(-1);
							_error.set_message("Exception from Python");
							SendEvent(_error.descriptor()->full_name(), _error.SerializeAsString());
						}
						break;
					}
					else 
					{
						_null_result = false;
						json _root = json::parse(output);
						//if (!_root.empty())		// it must always callback even empty
						{
							EvalViaShell::Result _result;
							_result.set_command(_command);
							_result.set_response(output);
							SendEvent(_result.descriptor()->full_name(), _result.SerializeAsString());
						}
					}
				}
				catch (const std::exception)
				{
					LOG_E(TAG, "Fail to parse response: %s", output.c_str());
					EvalViaShell::Error _error;
					_error.set_code(-1);
					_error.set_message("error message: " + output);
					SendEvent(_error.descriptor()->full_name(), _error.SerializeAsString());
					break;
				}
			}
			if (_script_filename != "")
			{
				try
				{ 
					std::filesystem::remove(_script_filename.c_str());
				}
				catch (const std::exception)
				{
					LOG_D(TAG, "\"%s\" cannot be removed, push into to-be-removed list", _script_filename.c_str());
					Array<String> _removed_file_list = ReadValue<Array<String>>("PythonAgent.removedFileList");
					_removed_file_list.push_back(_script_filename);
					WriteValue("PythonAgent.removedFileList", _removed_file_list);
				}
			}
			break;
		}
		default:
			break;
		}
	}

	String PythonAgent::ssystem(const String& command) {
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
				try
				{
					while (!file.eof()) result.push_back(file.get());
				}
				catch (const std::exception& e)
				{
					LOG_W(TAG, "exception from Python: %s", e.what());
				}
				file.close();
			}
		}
		catch (const std::exception& e)
		{
			LOG_W(TAG, "exception from Python: %s", e.what());
		}
		remove(tmpname);
		return result;
	}

	bool PythonAgent::findAndReplaceAll(String& data, const String& toSearch, const String& replaceStr)
	{
		bool _ret = false;
		// Get the first occurrence
		size_t pos = data.find(toSearch);
		String _replace_str = "";
		if (replaceStr.size() >= 2 && replaceStr.front() == '\"' && replaceStr.back() == '\"')
			_replace_str = replaceStr.substr(1, replaceStr.size() - 2);
		else
			_replace_str = replaceStr;
		// Repeat till end is reached
		while (pos != std::string::npos)
		{
			// Replace this occurrence of Sub String
			unsigned char _next_char = data[pos + toSearch.size()];
			if (!std::isalpha(_next_char) && !std::isdigit(_next_char) && _next_char != '_' && _next_char != '.' && _next_char != '['
				|| toSearch.substr(0, 3) == "::(" && toSearch.back() == ')'
				|| toSearch.back() == ']')				// for "::a1.b1.c1[x1][y1]::a2.b2.c2[x2][y2]" without space as delimeters
			{
				data.replace(pos, toSearch.size(), _replace_str);
				_ret = true;
				// Get the next occurrence from the current position
				pos = data.find(toSearch, pos + _replace_str.size());
			}
			else
			{
				//if (_next_char == '.')
				//	return false;
				//else
				pos = data.find(toSearch, pos + toSearch.size());
			}
		}
		return _ret;
	}

	String PythonAgent::EscapeJSONString(const String& in, bool escape_quote)
	{
		std::ostringstream o;
		for (auto c = in.cbegin(); c != in.cend(); c++) {
			switch (*c) {
			case '"': {
				if (escape_quote)
					o << "\\\"";
				break;
			}
			case '\b': o << "\\b"; break;
			case '\f': o << "\\f"; break;
			case '\n':
				o << "\\r\\n";
				break;
			case '\r': {
				if (c + 1 == in.cend() || *(c + 1) != '\n')
					o << "\\r\\n";
				break;
			}
			case '\t': o << "\\t"; break;
			case '\\': {
				if (c + 1 != in.cend())
				{
					switch (*(c + 1))
					{
					case '\\':
						if (escape_quote)
						{
							c++;
							o << "\\\\\\\\";
						}
						else
						{
							c++;
							o << "\\" << *c;
						}
						break;
					case '"':
						if (escape_quote)
						{
							c++;
							o << "\\\\\\\"";
						}
						else
						{
							c++;
							o << "\\" << *c;
						}
						break;
					case 'f':
					case 'n':
					case 'r':
					case 't':
						if (escape_quote)
						{
							c++;
							o << "\\\\" << *c;
						}
						else
						{
							c++;
							o << "\\" << *c;
						}
						break;
					case 'u':
					{
						o << "\\u";
						c += 2;
						for (int i = 0; i < 4 && c != in.cend(); i++, c++)
						{
							o << *c;
						};
						break;
					}
					default:
						if (escape_quote)
						{
							o << "\\\\";
						}
						else
						{
							o << "\\";
						}
						break;
					}
				}
				else
				{
					if (escape_quote)
					{
						o << "\\\\";
					}
					else
					{
						o << "\\";
					}
				}
				break;
			}
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
		return o.str();
	}

	void PythonAgent::EscapeJSON(const String& in, String& out)
	{
		out = "";
		size_t _start_pos = in.find_first_of('\"');
		if (_start_pos != String::npos && _start_pos > 0)
			out += in.substr(0, _start_pos);
		size_t _end_pos = _start_pos;
		while (_start_pos != String::npos)
		{
			while ((_end_pos = in.find_first_of('\"', _end_pos + 1)) != String::npos && in[_end_pos - 1] == '\\')
			{
			}
			if (_end_pos == String::npos)
				break;
			if (_end_pos - _start_pos > 1)
				out += "\"" + EscapeJSONString(in.substr(_start_pos + 1, _end_pos - _start_pos - 1), false) + "\"";
			else
				out += "\"\"";
			_start_pos = in.find_first_of('\"', _end_pos + 1);
			out += in.substr(_end_pos + 1, _start_pos - _end_pos - 1);
			_end_pos = _start_pos;
		}
		if (_start_pos != String::npos && _start_pos > 0)
			out += in.substr(_start_pos);
	}

	size_t PythonAgent::FindRightBracket(const String& target, size_t pos, const Pair<char, char>& bracket)
	{
		size_t _ret = pos;
		int _left_bracketcount = 1;
		int _right_bracket_count = 0;
		while (_right_bracket_count < _left_bracketcount)
		{
			_ret = target.find_first_of(bracket.second, _ret + 1);
			if (_ret < target.size())
			{
				_left_bracketcount = (int)std::count(target.begin() + pos, target.begin() + _ret + 1, bracket.first);
				_right_bracket_count = (int)std::count(target.begin() + pos, target.begin() + _ret + 1, bracket.second);
			}
			else
			{
				LOG_W(TAG, "Brackets are not paired for %s", target.c_str());
				break;
			}
		}
		return _ret;
	}

	void PythonAgent::ReplaceModelNameWithValue(String& target, const String& resolved_model_name)
	{
		if (target.find('\0') != String::npos)		// ignore binary data
			return;
		Stack<Pair<size_t, size_t>> _active_range;
		Set<Pair<size_t, size_t>> _ignored_range_list;
		Set<String> _ignored_token_list;
		Map<String, String> _resolved_model_cache;
		_active_range.push(std::make_pair(0, String::npos));
		while (!_active_range.empty())
		{
			size_t _start_pos = _active_range.top().first;
			size_t _end_pos = _active_range.top().second;
			size_t _range_end = _end_pos;
			while ((_start_pos = target.find("::", _start_pos)) < _range_end)
			{
				if (_start_pos > 0 && target[_start_pos - 1] == '`')
				{	// ignoring all "::" between ` `
					_start_pos = target.find('`', _start_pos);
					continue;
				}
				size_t _next_start = _start_pos;
				while (target.size() > _next_start + 2 && target[_next_start + 2] == ':')
					_next_start++;
				if (_next_start - _start_pos > 0)
				{
					if (_next_start - _start_pos > 1 && _ignored_range_list.find(std::make_pair(_start_pos, _range_end)) == _ignored_range_list.end())
					{
						_active_range.push(std::make_pair(_start_pos, _range_end));
						_ignored_range_list.insert(std::make_pair(_start_pos, _range_end));
					}
					_start_pos = _next_start;
				}
				if (target[_start_pos + 2] == '(')
				{
					size_t _token_end_pos = FindRightBracket(target, _start_pos + 1, std::make_pair('(', ')'));
					String _tobe_replaced = target.substr(_start_pos, _token_end_pos - _start_pos + 1);
					String _token = target.substr(_start_pos + 3, _token_end_pos - _start_pos - 3);
					size_t _model_pos = _token.find("::");
					if (_model_pos != String::npos && _ignored_range_list.find(std::make_pair(_start_pos + 3, _token_end_pos)) == _ignored_range_list.end())
					{
						if (_ignored_token_list.find(_token) == _ignored_token_list.end())
						{
							_active_range.push(std::make_pair(_start_pos + 3, _token_end_pos));
							_ignored_range_list.insert(std::make_pair(_start_pos + 3, _token_end_pos));
							_ignored_token_list.insert(_token);
							break;
						}
						else
						{
							_start_pos = _token_end_pos;
							continue;
						}
					}
					//ReplaceModelNameWithValue(_token);
					//action_eval_->Eval(((String)"Bio.Chromosome.DNA.Temp.Value=" + _token).c_str());
					//const std::regex NOT_BINARY_EXPR("^[^\t\n\x0B\f\r]*$");
					//const std::regex NOT_BINARY_EXPR("^[^\x0]*$");
					//if (_token.find("::") == String::npos && std::regex_match(_token, NOT_BINARY_EXPR))
					if (_token.find('\0') == String::npos && (_model_pos == String::npos || _model_pos > 1 && _token[_model_pos - 1] == '`'))
					{
						String _model_value;
						if (_token.empty()) {
							_model_value = "";
						}
						else if (_token.front() == '{' && _token.back() == '}'
							|| _token.front() == '[' && _token.back() == ']')
						{	// escaping JSON
							_model_value = EscapeJSONString(_token, true);
						}
						else
						{
							//LOG_E(TAG, "Unsupported operation!! expression: %s", _token.c_str());
							_model_value = _token;
						}
						if (findAndReplaceAll(target, _tobe_replaced, _model_value) == false)
						{
							LOG_E(TAG, "Unsupported arithmetic operation!! expression: %s", _token.c_str());
							break;
						}
					}
					else
					{
						_start_pos = _token_end_pos + 1;
					}
					continue;
				}
				_end_pos = target.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.", _start_pos + 2);
				String _model_value;
				bool _found_in_model = false;
				if (_end_pos == String::npos)
				{
					_end_pos = target.size();
					String _model_name = target.substr(_start_pos + 2, _end_pos - _start_pos - 2);
					if (!_model_name.empty())
					{
						_model_value = ReadValue<String>(_model_name);
						if (_model_value != String("::") + _model_name)
						{
							_found_in_model = true;
						}
					}
				}
				else if (target[_end_pos] == '[')
				{
					size_t _index_end_pos = FindRightBracket(target, _end_pos, std::make_pair('[', ']'));
					if (_index_end_pos == String::npos)
						_index_end_pos = target.size();
					if (_index_end_pos - _end_pos == 1)
					{	// getting size of array, JSON array or JSON object
						String _model_name;
						if (target.substr(_index_end_pos + 1, sizeof(".size") - 1) == ".size")
						{
							_model_name = target.substr(_start_pos + 2, _end_pos - _start_pos - 2);
							_index_end_pos = _end_pos + sizeof(".size");
						}
						else if (target.substr(_index_end_pos + 1, sizeof(".length") - 1) == ".length")
						{
							_model_name = target.substr(_start_pos + 2, _end_pos - _start_pos - 2);
							_index_end_pos = _end_pos + sizeof(".length");
						}
						if (!_model_name.empty())
						{
							Array<String> _value_list = ReadValue<Array<String>>(_model_name);
							if (_value_list.size() > 0
								&& (_value_list.front().size() == 0 || _value_list.front().front() != '{'
									|| _value_list.back().size() == 0 || _value_list.back().back() != '}'))
							{
								_model_value = std::to_string(_value_list.size());
								_found_in_model = true;
							}
							else
							{
								String _value = ReadValue<String>(_model_name);
								json _root;
								String _escaped_value = "";
								if (!_value.empty())
								{
									size_t _first_quote = _value.find('\"');
									if (_first_quote != String::npos && _first_quote > 1 && _value[_first_quote - 1] == '\\')
									{
										_value = std::regex_replace(_value, std::regex(R"(\\\")"), "\"");
									}
									try
									{
										_root = json::parse(_value);
									}
									catch (const std::exception& e)
									{
										LOG_D(TAG, "Error when parsing \"%s\" as JSON, message=%s", _value.c_str(), e.what());
										EscapeJSON(_value, _escaped_value);
									}
								}
								if (_escaped_value != "")
								{
									try
									{
										_root = json::parse(_escaped_value);
										LOG_I(TAG, "JSON string escaped");
									}
									catch (const std::exception& e)
									{
										LOG_E(TAG, "Escaped!! Error when parsing \"%s\" as JSON, message=%s", _escaped_value.c_str(), e.what());
									}
								}
								if (!_root.is_null())
								{	// JSON format
									if (_root.is_array() || _root.is_object())
									{
										_model_value = std::to_string(_root.size());
									}
									else
									{
										_model_value = "0";
									}
									_found_in_model = true;
								}
								else
								{
									_model_value = "0";
									_found_in_model = true;
								}
							}
						}
					}
					else
					{
						String _index_str = target.substr(_end_pos + 1, _index_end_pos - _end_pos - 1);
						if (_index_str.find("::") != String::npos && _ignored_range_list.find(std::make_pair(_end_pos + 1, _index_end_pos)) == _ignored_range_list.end())
						{
							if (_ignored_token_list.find(_index_str) == _ignored_token_list.end())
							{
								_active_range.push(std::make_pair(_end_pos + 1, _index_end_pos));
								_ignored_range_list.insert(std::make_pair(_end_pos + 1, _index_end_pos));
								_ignored_token_list.insert(_index_str);
								break;
							}
							else
							{
								_start_pos = _index_end_pos;
								continue;
							}
						}
						String _model_name = target.substr(_start_pos + 2, _end_pos - _start_pos - 2);
						if (!_model_name.empty())
						{
							Array<String> _value_list = ReadValue<Array<String>>(_model_name);
							if (_value_list.size() > 0
								&& (_value_list.front().size() == 0 || _value_list.front().front() != '{'
									|| _value_list.back().size() == 0 || _value_list.back().back() != '}'))
							{
								int _array_index = std::stoi(_index_str);
								if (_value_list.size() > _array_index)
								{
									_found_in_model = true;

									if (target.size() > _index_end_pos + 1 && target[_index_end_pos + 1] == '.')
									{
										LOG_E(TAG, "Unsupported operation!! model name: %s", _model_name.c_str());
									}
									else
									{
										_model_value = _value_list[_array_index];
									}
								}
								//_end_pos = _index_end_pos + 1;
							}
							else
							{
								String _value = ReadValue<String>(_model_name);

								if (resolved_model_name == _model_name)
								{
									if (!resolved_model_name.empty())
										LOG_W(TAG, "Not suppored recursive replacing of model name %s", _model_name.c_str());
									_start_pos = _end_pos;
									continue;
								}
								else
								{	// modified for replacing model name with [...] in JSON
									if (_resolved_model_cache.count(_model_name) > 0)
										_value = _resolved_model_cache[_model_name];
									else
									{
										ReplaceModelNameWithValue(_value, _model_name);
										_resolved_model_cache[_model_name] = _value;
									}
								}
								//_model_value = _root[_index_str];


								/*
									(sample 1, assign) ::JustDoIt.Project.item[items][1][items][4][content] ::JustDoIt.SourceCodeDescMap["::JustDoIt.GetProjectsByCondition.Result.sourceCode"]
									(sample 2, cond) ::YJInfo.Task.MemberCollection[::YJInfo.LayoutTemplate.layout[layout][0][items][MODIFYTASK_MEMBER][items][::index][id]] = "true"
									(sample 3, cond) ::index<::JustDoIt.LayoutTemplate.layout[layout][PRODUCER_PROJECTLIST_ROOT][items][PRODUCER_PROJECTITEM_ROOT][items][::group_index][items][].length - 1
								*/

								//size_t _last_right_bracket = target.rfind(']');
								//String _path_str = target.substr(_end_pos, _last_right_bracket - _end_pos + 1);
								//String _last_str = target.substr(_last_right_bracket + 1);
								size_t _path_end_pos = _index_end_pos;
								if (_path_end_pos < target.size() - 1)
									_path_end_pos++;
								else
									_path_end_pos = String::npos;
								//while ((_path_end_pos = target.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_\"", _path_end_pos)) != String::npos)
								while ((_path_end_pos = target.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_", _path_end_pos)) != String::npos)
								{
									if (target[_path_end_pos] == '[')
									{
										_path_end_pos = FindRightBracket(target, _path_end_pos, std::make_pair('[', ']'));
										if (_path_end_pos < target.size() - 1)
											_path_end_pos++;
										else
											_path_end_pos = String::npos;
									}
									else
									{
										size_t _last_bracket_pos = String::npos;
										if (target[_path_end_pos - 1] != ']')	// for "::a1.b1.c1[x1][y1]::a2.b2.c2[x2][y2]" without space as delimeters
										{
											_last_bracket_pos = target.rfind(']', _path_end_pos - 1);
											if (_last_bracket_pos > _end_pos)
												_path_end_pos = _last_bracket_pos + 1;
										}
										break;
									}
								};
								String _path_str = target.substr(_end_pos, _path_end_pos - _end_pos);
								/*
								if (_path_str.find("::") != String::npos && _ignored_range_list.find(std::make_pair(_end_pos, _path_end_pos)) == _ignored_range_list.end())
								{
									_active_range.push(std::make_pair(_end_pos, _path_end_pos));
									_ignored_range_list.insert(std::make_pair(_end_pos, _path_end_pos));
									break;
								}
								*/
								String _last_str = "";
								if (_path_end_pos < target.size())
									_last_str = target.substr(_path_end_pos);
								/*
								if (_last_str.find("::") != String::npos && _ignored_range_list.find(std::make_pair(_path_end_pos, String::npos)) == _ignored_range_list.end())
								{
									_active_range.push(std::make_pair(_path_end_pos, String::npos));
									_ignored_range_list.insert(std::make_pair(_path_end_pos, String::npos));
									break;
								}
								*/
								ReplaceModelNameWithValue(_path_str);
								ReplaceModelNameWithValue(_last_str);
								target = target.substr(0, _end_pos) + _path_str + _last_str;
								bool _is_get_size = false;
								bool _is_deep_path = false;
								if (std::count(_path_str.begin(), _path_str.end(), '[') > 1)
								{
									_is_deep_path = true;
									//size_t _path_end_pos = _path_str.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.\"[]");
									//size_t _path_end_pos = _path_str.rfind(']');
									if (_last_str.size() >= sizeof(".size") - 1 && _last_str.substr(0, sizeof(".size") - 1) == ".size")
									{
										_is_get_size = true;
										//_path_str = _path_str.substr(0, _path_end_pos);
										_index_end_pos = _end_pos + _path_str.size() + (sizeof(".size") - 1) - 1;
									}
									else if (_last_str.size() >= sizeof(".length") - 1 && _last_str.substr(0, sizeof(".length") - 1) == ".length")
									{
										_is_get_size = true;
										//_path_str = _path_str.substr(0, _path_end_pos);
										_index_end_pos = _end_pos + _path_str.size() + (sizeof(".length") - 1) - 1;
									}
									else
									{
										//_path_str = _path_str.substr(0, _path_end_pos);
										_index_end_pos = _end_pos + _path_str.size() - 1;
										//_index_end_pos = _end_pos + _path_str.size();
									}
								}
								else
								{
									_index_end_pos = _end_pos + _path_str.size() - 1;
								}
								_path_str.erase(std::remove(_path_str.begin(), _path_str.end(), '\"'), _path_str.end());
								_path_str.erase(std::remove(_path_str.begin(), _path_str.end(), ']'), _path_str.end());
								std::replace(_path_str.begin(), _path_str.end(), '[', '/');
								ReplacePathIndex(_path_str);
								if (_path_str.back() == '/')
									_path_str.pop_back();
								if (_is_deep_path == false)
								{
									if (_path_str.size() > sizeof(".size") && _path_str.substr(_path_str.size() - sizeof(".size") + 1, sizeof(".size") - 1) == ".size")
									{
										_path_str = _path_str.substr(0, _path_str.size() - sizeof(".size") + 1);
										_is_get_size = true;
									}
									else if (_path_str.size() > sizeof(".length") && _path_str.substr(_path_str.size() - sizeof(".length") + 1, sizeof(".length") - 1) == ".length")
									{
										_path_str = _path_str.substr(0, _path_str.size() - sizeof(".length") + 1);
										_is_get_size = true;
									}
								}
								if (_is_get_size == true)
								{
									//_model_name = target.substr(_start_pos + 2, _end_pos - _start_pos - 2);
									//if (!_model_name.empty())
									{
										json _root;
										String _escaped_value = "";
										if (!_value.empty())
										{
											size_t _first_quote = _value.find('\"');
											if (_first_quote != String::npos && _first_quote > 1 && _value[_first_quote - 1] == '\\')
											{
												_value = std::regex_replace(_value, std::regex(R"(\\\")"), "\"");
											}
											try
											{
												_root = json::parse(_value);
											}
											catch (const std::exception& e)
											{
												LOG_D(TAG, "Error when parsing \"%s\" as JSON, message=%s", _value.c_str(), e.what());
												EscapeJSON(_value, _escaped_value);
											}
										}
										if (_escaped_value != "")
										{
											try
											{
												_root = json::parse(_escaped_value);
												LOG_I(TAG, "JSON string escaped");
											}
											catch (const std::exception& e)
											{
												LOG_E(TAG, "Escaped!! Error when parsing \"%s\" as JSON, message=%s", _escaped_value.c_str(), e.what());
											}
										}
										if (!_root.is_null())
										{	// JSON format
											json::json_pointer _target_ptr(_path_str);
											if (_root[_target_ptr].is_array() || _root[_target_ptr].is_object())
											{
												_model_value = std::to_string(_root[_target_ptr].size());
											}
											else
											{
												_model_value = "0";
											}
											_found_in_model = true;
										}
									}
								}
								else
								{
									json _root;
									String _escaped_value = "";
									if (!_value.empty())
									{
										size_t _first_quote = _value.find('\"');
										if (_first_quote != String::npos && _first_quote > 1 && _value[_first_quote - 1] == '\\')
										{
											_value = std::regex_replace(_value, std::regex(R"(\\\")"), "\"");
										}
										try
										{
											_root = json::parse(_value);
										}
										catch (const std::exception& e)
										{
											LOG_D(TAG, "Error when parsing \"%s\" as JSON, message=%s", _value.c_str(), e.what());
											EscapeJSON(_value, _escaped_value);
										}
									}
									if (_escaped_value != "")
									{
										try
										{
											_root = json::parse(_escaped_value);
											LOG_I(TAG, "JSON string escaped");
										}
										catch (const std::exception& e)
										{
											LOG_E(TAG, "Escaped!! Error when parsing \"%s\" as JSON, message=%s", _escaped_value.c_str(), e.what());
										}
									}
									if (!_root.is_null())
									{	// JSON format
										String _orig_path_str = _path_str;
										while (true)
										{
											json::json_pointer _target_ptr(_path_str);
											try
											{
												if (_root[_target_ptr].is_null())
													_model_value = "";
												else if (_path_str == _orig_path_str)
													_model_value = _root[_target_ptr].dump();
												else
												{
													json _sub_tree = json::parse(_root[_target_ptr].get<String>());
													String _new_path = _orig_path_str.substr(_path_str.size());
													json::json_pointer _subtree_target_ptr(_new_path);
													_model_value = _sub_tree[_subtree_target_ptr].dump();
												}
												_found_in_model = true;
												break;
											}
											catch (json::exception)
											{
												size_t _pos = _path_str.find_last_of('/');
												if (_pos != String::npos && _pos != 0)
													_path_str = _path_str.substr(0, _pos);
												else
													break;
											}
										}
									}
								}
							}
						}
					}
					_end_pos = _index_end_pos + 1;
				}
				else if (target[_end_pos - 1] == '.')
				{
					if (_ignored_range_list.find(std::make_pair(_end_pos, _range_end)) == _ignored_range_list.end())
					{
						_active_range.push(std::make_pair(_end_pos, _range_end));
						_ignored_range_list.insert(std::make_pair(_end_pos, _range_end));
					}
				}
				else
				{
					String _model_name = target.substr(_start_pos + 2, _end_pos - _start_pos - 2);
					if (!_model_name.empty())
					{
						_model_value = ReadValue<String>(_model_name);
						if (_model_value != String("::") + _model_name)
						{
							if (resolved_model_name == _model_name)
							{
								if (!resolved_model_name.empty())
									LOG_W(TAG, "Not suppored recursive replacing of model name %s", _model_name.c_str());
							}
							else
							{
								if (_resolved_model_cache.count(_model_name) > 0)
									_model_value = _resolved_model_cache[_model_name];
								else
								{
									ReplaceModelNameWithValue(_model_value, _model_name);		// for nested model names
									_resolved_model_cache[_model_name] = _model_value;
								}
								_found_in_model = true;
							}
						}
					}
				}

				if (_found_in_model)
				{
					String _tobe_replaced = target.substr(_start_pos, _end_pos - _start_pos);
					if (findAndReplaceAll(target, _tobe_replaced, _model_value) == false)
					{
						LOG_E(TAG, "Unsupported nested protobuf format or string concatenation without any delimeter!! model name: %s", target.substr(_start_pos, _end_pos - _start_pos).c_str());
						break;
					}
					//_start_pos += _model_value.size();		// if replaced, search from the start for nested model names
				}
				else
				{
					_start_pos = _end_pos;
				}
			}
			if (_start_pos >= _range_end)
				_active_range.pop();
		}
	}

	void PythonAgent::ReplacePathIndex(String& path_str)
	{
		int _decimal_count = ReadValue<int>("Bio.Cell.Model.NumberWithDecimalCount");
		WriteValue("Bio.Cell.Model.NumberWithDecimalCount", 0);
		// find and eval indexes
		size_t _left = path_str.find_first_not_of('/');
		size_t _right = path_str.find('/', _left);
		while (_left != String::npos)
		{
			WriteValue("Bio.Chromosome.DNA.Temp.Index", "");
			String _index_str = path_str.substr(_left, _right - _left);
			/*
			if (_index_str != "" && cond_eval_->findOperator(_index_str) == false)
			{
				//action_eval_->Eval(((String)"Bio.Chromosome.DNA.Temp.Index=" + _index_str).c_str());
				action_eval_->Eval(_index_str, "Bio.Chromosome.DNA.Temp.Index");
				String _new_index_str = ReadValue<String>("Bio.Chromosome.DNA.Temp.Index");
				if (_new_index_str != _index_str)
					path_str.replace(path_str.find(_index_str), _index_str.size(), _new_index_str);
			}
			*/
			if (_right != String::npos)
			{
				_left = _right + 1;
				_right = path_str.find('/', _left);
			}
			else
			{
				_left = _right;
			}
		}
		WriteValue("Bio.Cell.Model.NumberWithDecimalCount", _decimal_count);
	}
}