#include "VuetifyKits.h"
#include "../proto/VuetifyKits.pb.h"
#include <google/protobuf/util/json_util.h>
#include <regex>
#include <iomanip>

using json = nlohmann::json;

#define TAG "VuetifyKits"

namespace VuetifyKits
{

#ifndef STATIC_API
	extern "C" PUBLIC_API BioSys::RNA * CreateInstance(BioSys::IBiomolecule * owner)
	{
		return new VuetifyKits(owner);
	}
#endif

	Map<String, Mutex> VuetifyKits::file_reader_mutex_;

	VuetifyKits::VuetifyKits(BioSys::IBiomolecule* owner)
		:RNA(owner, "VuetifyKits", this)
	{
		owner->init("*");		// to receive all messages of whole namespaces
		init();
	}

	VuetifyKits::~VuetifyKits()
	{
	}

	inline String VuetifyKits::GetEventSourcePath()
	{
		return ReadValue<String>("Bio.Cell.Current.EventSource");
	}

	void VuetifyKits::OnEvent(const DynaArray& name)
	{
		USING_BIO_NAMESPACE
		const String& _name = name.str();
		switch (hash(_name))
		{
		case "VuetifyKits.Send"_hash:
		{
			String _content = ReadValue<String>("VuetifyKits.Send.view");
			bool _show_result = ReadValue<bool>("VuetifyKits.Send.show_result");
			bool _subView = false;
			if (_content == "")
			{
				_content = ReadValue<String>("VuetifyKits.Send.subView");
				if (_content != "")
				{
					_subView = true;
				}
				else
				{
					LOG_E(TAG, "value of \"VuetifyKits.Send.layout\" is empty");
					return;
				}
			}

			using namespace nlohmann;
			json _view_json;
			String _escaped_value = "";
			try
			{
				_view_json = json::parse(_content);
			}
			catch (const std::exception& e)
			{
				LOG_D(TAG, "Invalid JSON format!\n    Exception:%s\n    content: %s", e.what(), _content.c_str());
				EscapeJSON(_content, _escaped_value);
			}
			if (_escaped_value != "")
			{
				try
				{
					_view_json = json::parse(_escaped_value);
					LOG_I(TAG, "JSON string escaped");
				}
				catch (const std::exception& e)
				{
					LOG_E(TAG, "Escaped!! Error when parsing \"%s\" as JSON, message=%s", _escaped_value.c_str(), e.what());
					return;
				}
			}

			// converting some specific columns to string
			recursive_iterate(_view_json, [](json::iterator it) {});

			String _view = _view_json.dump();

			Layout _layout;
			using namespace google::protobuf;
			if (!util::JsonStringToMessage(_view, &_layout).ok())
			{
				LOG_E(TAG, "Unable to convert JSON to Protobuf message \"%s\"!\n    content: %s", Layout::descriptor()->full_name().c_str(), _view.c_str());
				return;
			}
			else
			{
				// get scripts from files
				for (int i = 0; i < _layout.methodlist().size(); i++)
				{
					String _script_filename = _layout.methodlist(i).functionvalue();
					ReadScriptFile(_script_filename, *_layout.mutable_methodlist(i)->mutable_functionvalue());
				}
				if (!_subView)
				{
					if (_show_result)
						LOG_I(TAG, "Send VuetifyKits.Layout() %s", _view.c_str());
					SendEvent(Layout::descriptor()->full_name(), _layout.SerializeAsString());
				}
				else
				{
					if (_show_result)
						LOG_I(TAG, "Send VuetifyKits.PartialLayout() %s", _view.c_str());
					SendEvent(PartialLayout::descriptor()->full_name(), _layout.SerializeAsString());
				}
			}
			break;
		}
		case "VuetifyKits.AddTreeView"_hash:
		{
			String _id = ReadValue<String>("VuetifyKits.AddTreeView.id");
			String _parentId = ReadValue<String>("VuetifyKits.AddTreeView.parentId");
			String _content = ReadValue<String>("VuetifyKits.AddTreeView.content");
			String _target_model_name = ReadValue<String>("VuetifyKits.AddTreeView.target_model_name");
			bool _renew = ReadValue<bool>("VuetifyKits.AddTreeView.renew");
			if (_renew)
			{
				tree_view_ = json::array();
				tree_view_map_.clear();
			}
			if (_parentId == "null")
			{
				tree_view_map_[_id] = json::json_pointer("/"+ std::to_string(tree_view_.size()));
				tree_view_.push_back(json::object({ { "id",_id }, { "content",_content }, { "items", json::array() } }));
				CheckUnknownItems(_id);
				if (!_target_model_name.empty())
				{
					WriteValue(_target_model_name, tree_view_.dump());
				}
			}
			else if (tree_view_map_.find(_parentId) != tree_view_map_.end())
			{
				json::json_pointer _parent_pos = tree_view_map_[_parentId];
				json& _items = tree_view_[_parent_pos]["items"];
				_items.push_back(json::object({ { "id",_id }, { "content",_content }, { "items", json::array() } }));
				json::json_pointer _child_pos = _parent_pos;
				_child_pos.push_back("items");
				_child_pos.push_back(std::to_string(_items.size() - 1));
				tree_view_map_[_id] = _child_pos;
				CheckUnknownItems(_id);
				if (!_target_model_name.empty())
				{
					WriteValue(_target_model_name, tree_view_.dump());
				}
			}
			else
			{
				if (unknown_tree_item_map_.count(_parentId) == 0)
					unknown_tree_item_map_[_parentId] = json::array();
				unknown_tree_item_map_[_parentId].push_back(json::object({ { "id",_id }, { "content",_content }, { "items", json::array() } }));
			}
			break;
		}
		case "VuetifyKits.ModifyTreeView"_hash:
		{
			String _id = ReadValue<String>("VuetifyKits.ModifyTreeView.id");
			String _key = ReadValue<String>("VuetifyKits.ModifyTreeView.key");
			String _value = ReadValue<String>("VuetifyKits.ModifyTreeView.value");
			String _target_model_name = ReadValue<String>("VuetifyKits.ModifyTreeView.target_model_name");
			if (!_target_model_name.empty())
			{
				 json _tree_view = ReadValue<json>(_target_model_name);
				 if (_tree_view.empty())
					 _tree_view = tree_view_;
				_tree_view[tree_view_map_[_id]][_key] = _value;
				tree_view_ = _tree_view;
				WriteValue(_target_model_name, tree_view_.dump());
			}
			break;
		}
		default:
			break;
		}
	}

	template<class UnaryFunction>
	void VuetifyKits::recursive_iterate(nlohmann::json& j, UnaryFunction f)
	{
		for (auto it = j.begin(); it != j.end(); ++it)
		{
			if (it->is_structured())
			{
				try
				{
					if (j.is_object() && (it.key() == "recordset" || it.key() == "userData" || it.key() == "data" || it.key() == "content"
						|| it.key() == "min" || it.key() == "max"))
					{
						*it = it->dump();
					}
					else
					{
						recursive_iterate(*it, f);
					}
				}
				catch (const std::exception& e)
				{
					LOG_E(TAG, "Unknown error!\n    excpetion: %s\n    content=%s", e.what(), j.dump().c_str());
					return;
				}
			}
			else if (j.is_object() && it.value().is_string() && (it.key() == "items" || it.key() == "methodList"))
			{
				using namespace nlohmann;
				String _value = it.value().get<String>();
				if (_value == "")
					*it = json::array();
				else
				{
					size_t _start_pos = 0;
					_start_pos = _value.find(String("\"") + it.key() + "\":", _start_pos);
					if (_start_pos != String::npos)
						_start_pos = _value.find_first_not_of(' ', _start_pos + (String("\"") + it.key() + "\":").length());
					while (_start_pos != String::npos)
					{
						if (_value.size() > _start_pos + 1 && _value[_start_pos] == '\"' && _value[_start_pos + 1] == '[')
						{
							size_t _end_pos = FindRightBracket(_value, _start_pos + 1, std::make_pair('[', ']'));
							if (_end_pos != String::npos && _value.size() > _end_pos + 1 && _value[_end_pos + 1] == '\"')
							{
								_value.replace(_start_pos, _end_pos - _start_pos + 2, UnescapeJSONString(_value.substr(_start_pos + 1, _end_pos - _start_pos)));
							}
						}
						_start_pos = _value.find(String("\"") + it.key() + "\":", _start_pos);
						if (_start_pos != String::npos)
							_start_pos = _value.find_first_not_of(' ', _start_pos + (String("\"") + it.key() + "\":").length());
					}
					String _escaped_value = "";
					try
					{
						*it = json::parse(_value);
						recursive_iterate(*it, f);
					}
					catch (const std::exception& e)
					{
						LOG_D(TAG, "Error when parsing \"%s\" as JSON, message=%s", _value.c_str(), e.what());
						EscapeJSON(_value, _escaped_value);
					}
					if (_escaped_value != "")
					{
						try
						{
							*it = json::parse(_escaped_value);
							LOG_I(TAG, "JSON string escaped");
							recursive_iterate(*it, f);
						}
						catch (const std::exception& e)
						{
							LOG_E(TAG, "Escaped!! Error when parsing \"%s\" as JSON, message=%s", _escaped_value.c_str(), e.what());
						}
					}
				}
			}
			else
			{
				f(it);
			}
		}
	}
	void VuetifyKits::ReadScriptFile(const String& filename, String& output)
	{
		MutexLocker _lock(file_reader_mutex_[filename]);
		output = "";
		FILE* _file = nullptr;
		try
		{
			_file = fopen((GetEventSourcePath() + filename).c_str(), "rb");
			if (_file != nullptr) { LOG_D(TAG, "FileIO.Read(%s) sucessfully", (GetEventSourcePath() + filename).c_str()); }
			else { LOG_D(TAG, "FileIO.Read(%s) failed", (GetEventSourcePath() + filename).c_str()); };
			if (_file == nullptr)
			{
				_file = fopen((GetRootPath() + filename).c_str(), "rb");
				if (_file != nullptr) { LOG_D(TAG, "FileIO.Read(%s) sucessfully", (GetRootPath() + filename).c_str()); }
				else { LOG_D(TAG, "FileIO.Read(%s) failed", (GetRootPath() + filename).c_str()); };
			}
			if (_file == nullptr)
			{
				_file = fopen(filename.c_str(), "rb");
				if (_file != nullptr) { LOG_D(TAG, "FileIO.Read(%s) sucessfully", filename.c_str()); }
				else { LOG_D(TAG, "FileIO.Read(%s) failed", filename.c_str()); };
			}
			if (_file != nullptr)
			{
				fseek(_file, 0, SEEK_END);
				size_t _size = ftell(_file);
				fseek(_file, 0, SEEK_SET);
				output.assign(_size, 0);
				_size = fread((void*)output.data(), sizeof(unsigned char), _size, _file);
				fclose(_file);
				ReplaceModelNameWithValue(output, "", false);
			}
			else
			{
				LOG_W(TAG, "fail to open/read file %s", filename.c_str());
			}
		}
		catch (const std::exception& e)
		{
			if (_file != nullptr)
				fclose(_file);
			LOG_W(TAG, "exception (%s) thrown by fopen/fread(%s)", e.what(), filename.c_str());
		}
	}

	String VuetifyKits::EscapeJSONString(const String& in, bool escape_quote)
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
	void VuetifyKits::EscapeJSON(const String& in, String& out)
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
	String VuetifyKits::UnescapeJSONString(const String& in)
	{
		std::ostringstream o;
		for (auto c = in.cbegin(); c != in.cend(); c++) {
			if (*c == '\\' && c + 1 != in.cend())
			{
				c++;
				switch (*c) {
				case '"': {
					o << "\"";
					break;
				}
				case 'b':
					o << "\b";
					break;
				case 'f':
					o << "\f";
					break;
				case 'r':
					o << "\r";
					break;
				case 'n':
					o << "\n";
					break;
				case 't':
					o << "\t";
					break;
				case '\\':
					o << "\\";
					break;
				case 'u':
				{
					char chr[2];
					for (int i = 0; i < 4 && c != in.cend(); i += 2)
					{
						String byte = { *(c + i), *(c + i + 1) };
						chr[i / 2] = (char)(int)strtol(byte.c_str(), nullptr, 16);
					}
					if (chr[0] == '\0' && '\x00' <= chr[1] && chr[1] <= '\x1f')
					{
						c += 6;
						o << chr[1];
					}
					else
					{
						o << "\\u";
						c += 2;
						for (int i = 0; i < 4 && c != in.cend(); i++, c++)
						{
							o << *c;
						};
					}
					break;
				}
				default:
					o << *c;
				}
			}
			else
				o << *c;
		}
		return o.str();
	}

	size_t VuetifyKits::FindRightBracket(const String& target, size_t pos, const Pair<char, char>& bracket)
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

	bool VuetifyKits::findAndReplaceAll(String& data, const String& toSearch, const String& replaceStr)
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

	void VuetifyKits::ReplaceModelNameWithValue(String& target, const String& resolved_model_name, bool recursive)
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
										if (recursive)
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
								if (recursive)
								{
									ReplaceModelNameWithValue(_path_str);
									ReplaceModelNameWithValue(_last_str);
								}
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
									if (recursive)
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
					if (!recursive)
						_start_pos += _model_value.size() - (sizeof("::") - 1);		// if replaced, search from the start for nested model names
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

	void VuetifyKits::ReplacePathIndex(String& path_str)
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
	void VuetifyKits::CheckUnknownItems(const String& id)
	{
		auto it = unknown_tree_item_map_.find(id);
		if (it != unknown_tree_item_map_.end())
		{
			json::json_pointer _parent_pos = tree_view_map_[id];
			json& _items = tree_view_[_parent_pos]["items"];
			for (int i = 0; i < it->second.size(); i++)
			{
				_items.emplace_back(it->second[i]);
				json::json_pointer _child_pos = _parent_pos;
				_child_pos.push_back("items");
				_child_pos.push_back(std::to_string(_items.size() - 1));
				const String& _id = _items.back()["id"];
				tree_view_map_[_id] = _child_pos;
				CheckUnknownItems(_id);
			}
			unknown_tree_item_map_.erase(it);
		}
	}
}