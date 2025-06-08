#include "Database.h"
#include "uuid.h"

#define TAG "Database"
#undef IN
#undef DELETE

namespace Database
{

#ifndef STATIC_API
	extern "C" PUBLIC_API BioSys::RNA * CreateInstance(BioSys::IBiomolecule * owner)
	{
		return new Database(owner);
	}
#endif

	const char Database::EQUAL_UTF8[] = { (char)0xef,(char)0xbc,(char)0x9d, (char)0x0 };
	const char Database::GREATER_UTF8[] = { (char)0xef,(char)0xbc,(char)0x9e, (char)0x0 };
	const char Database::LESS_UTF8[] = { (char)0xef,(char)0xbc,(char)0x9c, (char)0x0 };
	const char Database::PLUS_UTF8[] = { (char)0xef,(char)0xbc,(char)0x8b, (char)0x0 };
	const char Database::MINUS_UTF8[] = { (char)0xef,(char)0xbc,(char)0x8d, (char)0x0 };
	const char Database::NOT_EQUAL_UTF8[] = { (char)0xe2,(char)0x89,(char)0xa0, (char)0x0 };
	const char Database::GREATER_EQUAL_UTF8[] = { (char)0xe2,(char)0x89,(char)0xa7, (char)0x0 };
	const char Database::LESS_EQUAL_UTF8[] = { (char)0xe2,(char)0x89,(char)0xa6, (char)0x0 };
	const char Database::PRODUCT_UTF8[] = { (char)0xc3,(char)0x97, (char)0x0 };
	const char Database::DIVIDE_UTF8[] = { (char)0xc3,(char)0xb7, (char)0x0 };

	Database::Database(BioSys::IBiomolecule* owner)
		:RNA(owner, "Database", this)
		, owner_(owner)
	{
		if (ReadValue<bool>("Database.is_global_instance") == true)
			owner->init("*");		// to receive all messages of whole namespaces
		init();
	}

	Database::~Database()
	{
	}

	void Database::OnEvent(const DynaArray& name)
	{
		USING_BIO_NAMESPACE
			bool _is_active_instance = (ReadValue<bool>("Database.local_instance_only") == false) || (owner_->name() != "*");
		if (!_is_active_instance)
			return;
		String _name = name.str();
		switch (hash(_name))
		{
		case "Database.Config"_hash:
		{
			doConfig();
			break;
		}
		case "Database.Query"_hash:
		{
			WriteValue<String>("Database.Internal.Conjunction", "");
			doQuery();
			String _current_state = ReadValue<String>("Bio.Cell.Current.State");
			int _state_depth = (int)std::count(_current_state.begin(), _current_state.end(), '/') + 1;
			WriteValue("Database.Internal.LastDepth", _state_depth);
			break;
		}
		case "Database.AddCond"_hash:
		{
			doAddCond();
			break;
		}
		case "Database.Insert"_hash:
		{
			WriteValue<String>("Database.Internal.Conjunction", "");
			doUpdate(Const_DB::INSERT, "Insert", "INSERT INTO");
			String _current_state = ReadValue<String>("Bio.Cell.Current.State");
			int _state_depth = (int)std::count(_current_state.begin(), _current_state.end(), '/') + 1;
			WriteValue("Database.Internal.LastDepth", _state_depth);
			break;
		}
		case "Database.Remove"_hash:
		{
			WriteValue<String>("Database.Internal.Conjunction", "");
			doRemove();
			String _current_state = ReadValue<String>("Bio.Cell.Current.State");
			int _state_depth = (int)std::count(_current_state.begin(), _current_state.end(), '/') + 1;
			WriteValue("Database.Internal.LastDepth", _state_depth);
			break;
		}
		case "Database.Update"_hash:
		{
			WriteValue<String>("Database.Internal.Conjunction", "");
			doUpdate(Const_DB::UPDATE, "Update", "UPDATE ");
			String _current_state = ReadValue<String>("Bio.Cell.Current.State");
			int _state_depth = (int)std::count(_current_state.begin(), _current_state.end(), '/') + 1;
			WriteValue("Database.Internal.LastDepth", _state_depth);
			break;
		}
		case "AND"_hash:
		{
			//WriteValue("Database.Internal.Conjunction", " AND ");
			break;
		}
		case "OR"_hash:
		{
			WriteValue("Database.Internal.Conjunction", " OR ");
			break;
		}
		case "Database.GenerateUUID"_hash:
		{
			String _target_model = ReadValue<String>(_name + ".target_model_name");
			if (_target_model != "")
				WriteValue(_target_model, uuids::to_string(uuids::uuid_system_generator()()));
			break;
		}
		default:
			break;
		}
	}

	void Database::doQuery()
	{
		LOG_D(TAG, "Database::doQuery");
		WriteValue("Database.Internal.ProcType", Const_DB::QUERY);

		//WriteValue("Database.Internal.ConditionList", Array<String>());
		//WriteValue("Database.Internal.ModifierList", Array<String>());
		//WriteValue("Database.Internal.ParameterList", Array<String>());		// it may have something added by Database.function()
		//WriteValue("Database.Internal.Name", "");

		Array<String> _statement;
		if (ReadValue<bool>("Database.Query.difference_only") == true)
			_statement.push_back("SELECT DISTINCT ");
		else
			_statement.push_back("SELECT ");

		String _model_list_str = ReadValue<String>("Database.Query.model_list");
		Array<String> _model_list;
		Split(_model_list_str.substr(1, _model_list_str.size() - 2), _model_list, ",", Array<Pair<String, String>>({ std::make_pair("(",")") }));
		//Array<String> _model_list = ReadValue<Array<String>>("Database.Query.model_list");
		if (_model_list.size() == 1)
		{
			Set<String> _filter_list;
			_model_list[0] = trim(_model_list[0]);
			GetParameter(_model_list[0], _filter_list, String(1, DYNAMIC_CONDITION_PREFIX));
			if (_filter_list.size() > 0)
			{
				Array<String> _orig_model_list(_model_list);
				_model_list.clear();
				_model_list.push_back("*");
				for (auto _model_name : _orig_model_list)
				{
					if (_filter_list.find(DYNAMIC_CONDITION_PREFIX + _model_name.substr(3, _model_name.size() - 4)) == _filter_list.end())
						_model_list.push_back(_model_name);
				}
			}
			WriteValue("Database.Internal.FilterList", Array<String>(_filter_list.begin(), _filter_list.end()));
		}
		else
		{
			Array<String> _param_list_stored = ReadValue<Array<String>>("Database.Internal.ParameterList");
			Set<String> _param_list(_param_list_stored.begin(), _param_list_stored.end());
			int _param_list_size = (int)_param_list.size();

			for (int i = 0; i < _model_list.size(); i++)
			{
				_model_list[i] = trim(_model_list[i]);
				_model_list[i] = GetParameter(_model_list[i], _param_list, "");
			}
			if (_param_list.size() != _param_list_size)
				WriteValue("Database.Internal.ParameterList", Array<String>(_param_list.begin(), _param_list.end()));
		}
		std::unordered_set<String> _table_list;
		for (int i = 0; i < _model_list.size(); i++)
			RetrieveTableName(_model_list[i], _table_list);

		WriteValue("Database.Internal.Columns", _model_list);
		WriteValue("Database.Internal.TableList", Array<String>(_table_list.begin(), _table_list.end()));
		WriteValue("Database.Internal.Statement", _statement);
	}

	void Database::doAddCond()
	{
		LOG_D(TAG, "Database::doAddCond()");
		// _add_cond is the instance of message you can use.
		Array<String> _table_list_stored = ReadValue<Array<String>>("Database.Internal.TableList");
		std::unordered_set<String> _table_list(_table_list_stored.begin(), _table_list_stored.end());
		int _table_list_size = (int)_table_list.size();
		Array<String> _param_list_stored = ReadValue<Array<String>>("Database.Internal.ParameterList");
		Set<String> _param_list(_param_list_stored.begin(), _param_list_stored.end());
		int _param_list_size = (int)_param_list.size();

		bool _retrieve_tablename = true;
		bool _as_where_conditions = true;
		bool _as_modifier = false;
		bool _splitted = true;
		String _prefix = "";

		Array<String> _var;
		for (int i = 1; i <= 3; i++)
			_var.push_back(ReadValue<String>(String("Database.AddCond.var") + std::to_string(i)));

		String _event_name = ReadValue<String>("Bio.Cell.Current.Event");
		String _op_str = ReadValue<String>("Database.AddCond.op");
		String _op2_str = "";
		int _op = -1;
		if (_op_str == "=" || _op_str == "==" || _op_str == EQUAL_UTF8)
			_op = Const_DB::EQUAL;
		else if (_op_str == "!=" || _op_str == "<>" || _op_str == NOT_EQUAL_UTF8)
			_op = Const_DB::NOT_EQUAL;
		else if (_op_str == ">=" || _op_str == GREATER_EQUAL_UTF8)
			_op = Const_DB::GREATER_EQUAL;
		else if (_op_str == ">" || _op_str == GREATER_UTF8)
			_op = Const_DB::GREATER;
		else if (_op_str == "<=" || _op_str == LESS_EQUAL_UTF8)
			_op = Const_DB::LESS_EQUAL;
		else if (_op_str == "<" || _op_str == LESS_UTF8)
			_op = Const_DB::LESS;
		else
		{
			const google::protobuf::EnumValueDescriptor* _enum_desc = Private::OperatorConvertor::descriptor()->FindFieldByName("op")->enum_type()->FindValueByName(_op_str);
			if (_enum_desc != nullptr)
				_op = _enum_desc->number();
		}

		String _conjunction = ReadValue<String>("Database.Internal.Conjunction");
		bool _keep_conjunction = false;
		switch (_op)
		{
		case Const_DB::TABLE:
			_retrieve_tablename = false;
			_as_where_conditions = false;
			_conjunction = "";
			_keep_conjunction = true;
			break;
		case Const_DB::HAVING:
		case Const_DB::GROUP_BY:
		case Const_DB::ORDER_BY:
		case Const_DB::PAGING:
			_retrieve_tablename = true;
			_as_where_conditions = false;
			_as_modifier = true;
			_conjunction = "";
			_keep_conjunction = true;
			break;
		case Const_DB::DYNAMIC_CONDITIONS:
			_retrieve_tablename = true;
			_as_where_conditions = true;
			_splitted = true;
			_prefix = DYNAMIC_CONDITION_PREFIX;
			//_conjunction = "";
			_keep_conjunction = false;
			break;
		case Const_DB::NOT_EXISTS:
		case Const_DB::EXISTS:
			_retrieve_tablename = false;		// _retrieve_tablename = true; removed for that table_name in subclause must not be exported.
												//    ex. SELECT `XXX`.`User`.userId, `XXX`.`User`.account FROM `XXX`.`User`, `XXX`.`Assignment` WHERE  NOT EXISTS (SELECT `XXX`.`Thread`.ownerId FROM `XXX`.`Thread` WHERE `XXX`.`Assignment`.userId = `XXX`.`Thread`.ownerId AND ...) AND `XXX`.`Assignment`.userId = `XXX`.`User`.userId;
			_as_where_conditions = true;
			_splitted = true;
			_prefix = "";
			//_conjunction = "";
			_keep_conjunction = false;
		default:
			break;
		}

		if (!_keep_conjunction)
			WriteValue<String>("Database.Internal.Conjunction", " AND ");

		String _last_state = ReadValue<String>("Database.Internal.LastState");
		String _current_state = ReadValue<String>("Bio.Cell.Current.State");
		Array<String> _cond_list = ReadValue<Array<String>>("Database.Internal.ConditionList");
		int _last_state_depth = ReadValue<int>("Database.Internal.LastDepth");
		String _last_state_parent = _last_state.substr(0, _last_state.rfind('/'));
		String _current_state_parent = _current_state.substr(0, _current_state.rfind('/'));
		int _current_state_depth = (int)std::count(_current_state.begin(), _current_state.end(), '/') + 1;
		int _ancestor_depth = GetAncestorDepth(_last_state_parent, _current_state_parent);
		if (_ancestor_depth == 0)
			_ancestor_depth = _last_state_depth - 1 >= 0 ? _last_state_depth - 1 : 0;
		for (int i = 1; i < _last_state_depth - _ancestor_depth; i++)
			_cond_list.push_back(")");
		if (_conjunction != "")
			_cond_list.push_back(_conjunction);
		for (int i = 1; i < _current_state_depth - _ancestor_depth; i++)
			_cond_list.push_back("(");
		switch (_op)
		{
		case Const_DB::EQUAL:
		{
			if (_var[1] == "null")
				_op_str = " IS ";
			else
				_op_str = " = ";
			break;
		}
		case Const_DB::NOT_EQUAL:
			if (_var[1] == "null")
				_op_str = " IS NOT ";
			else
				_op_str = " != ";
			break;
		case Const_DB::GREATER_EQUAL:
			_op_str = " >= ";
			break;
		case Const_DB::GREATER:
			_op_str = " > ";
			break;
		case Const_DB::LESS_EQUAL:
			_op_str = " <= ";
			break;
		case Const_DB::LESS:
			_op_str = " < ";
			break;
		case Const_DB::HAVING:
			_op_str = " HAVING ";
			break;
		case Const_DB::LIKE:
			_op_str = " LIKE ";
			break;
		case Const_DB::NOT_LIKE:
			_op_str = " NOT LIKE ";
			break;
		case Const_DB::NOT_EXISTS:
			_op_str = " NOT EXISTS ";
			break;
		case Const_DB::EXISTS:
			_op_str = " EXISTS ";
			break;
			//case order_by:
			//  _op = " ORDER BY ";
			//  break;
		case Const_DB::BETWEEN:
			_op_str = " BETWEEN ";
			_op2_str = " AND ";
			break;
		case Const_DB::IN:
		{
			_op_str = " IN ";
			String& _content_list = _var[1];
			if (_content_list.size() >= 2 && _content_list.front() == '[' && _content_list.back() == ']')
			{
				_content_list.front() = '(';
				_content_list.back() = ')';
			}
			break;
		}
		case Const_DB::NOT_IN:
		{
			_op_str = " NOT IN ";
			String& _content_list = _var[1];
			if (_content_list.size() >= 2 && _content_list.front() == '[' && _content_list.back() == ']')
			{
				_content_list.front() = '(';
				_content_list.back() = ')';
			}
			break;
		}
		case Const_DB::GROUP_BY:
		{
			_op_str = " GROUP BY ";
			String& _content_list = _var[1];
			if (_content_list[0] == '[')
				_content_list = _content_list.substr(1, _content_list.size() - 2);
			break;
		}
		case Const_DB::ORDER_BY:
		{
			_op_str = " ORDER BY ";
			_op2_str = " ";
			String& _content_list = _var[1];
			if (_content_list[0] == '[')
				_content_list = _content_list.substr(1, _content_list.size() - 2);
			if (_var[2] != "")
			{
				Const_DB::SORT_TYPE _sort_type;
				if (Const_DB::SORT_TYPE_Parse(_var[2], &_sort_type) == true)
				{
					switch (_sort_type)
					{
					case Const_DB::DESCENDING:
						_var[2] = "DESC";
						break;
					default:
						_var[2] = "ASC";
					}
				}
				else
				{
					_var[2] = "ASC";
				}
			}
			break;
		}
		case Const_DB::TABLE:
		{
			String& _table_name_list = _var[1];
			if (_table_name_list[0] == '[')
				_table_name_list = _table_name_list.substr(1, _table_name_list.size() - 2);
			Array<String> _tables;
			Split(_table_name_list, _tables, ",", Array<Pair<String, String>>({ std::make_pair("(",")") }));
			for (auto _table_name : _tables)
			{
				if (_table_name.find("::") != 0)
					_table_list.insert(_table_name);
				else
				{
					_table_list.insert(ReadValue<String>(_table_name.substr(2)));
				}
			}
			break;
		}
		case Const_DB::PAGING:
		{
			if (_var[1] == "" && _var[2] != "")
			{
				_op_str = " PAGING ";
				_op2_str = " SIZE ";
				_var[1] = "0";
			}
			else if (_var[1] != "")
			{
				_op_str = " PAGING ";
				_op2_str = " SIZE ";
				if (_var[2] == "")
				{
					_var[2] = _var[1];
					_var[1] = "0";
				}
			}
			break;
		}
		case Const_DB::DYNAMIC_CONDITIONS:
		default:
			_op_str = "";
			break;
		}

		if (_retrieve_tablename)
		{
			_var[2] = GetParameter(_var[2], _param_list, _prefix);
			RetrieveTableName(_var[2], _table_list);
			_var[1] = GetParameter(_var[1], _param_list, _prefix);
			RetrieveTableName(_var[1], _table_list);
			_var[0] = GetParameter(_var[0], _param_list, _prefix);
			RetrieveTableName(_var[0], _table_list);
		}

		if (_table_list.size() != _table_list_size)
		{
			WriteValue("Database.Internal.TableList", Array<String>(_table_list.begin(), _table_list.end()));
		}
		if (_param_list.size() != _param_list_size)
		{
			WriteValue("Database.Internal.ParameterList", Array<String>(_param_list.begin(), _param_list.end()));
		}

		if (_as_where_conditions)
		{
			for (int i = 0; i < 3; i++)
				_var[i] = RemoveAlias(_var[i]);
			if (_splitted == false)
				_cond_list.push_back(_var[0] + _op_str + _var[1] + _op2_str + _var[2]);
			else
			{
				if (_var[0] != "")
					_cond_list.push_back(_var[0]);
				if (_op_str != "")
					_cond_list.push_back(_op_str);
				if (_var[1] != "")
					_cond_list.push_back(_var[1]);
				if (_op2_str != "")
					_cond_list.push_back(_op2_str);
				if (_var[2] != "")
					_cond_list.push_back(_var[2]);
			}
		}
		WriteValue("Database.Internal.ConditionList", _cond_list);

		Array<String> _modifier_list = ReadValue<Array<String>>("Database.Internal.ModifierList");
		if (_as_modifier)
		{
			if (_var[0] != "")
				_modifier_list.push_back(_var[0]);
			if (_op_str != "")
				_modifier_list.push_back(_op_str);
			if (_var[1] != "")
				_modifier_list.push_back(_var[1]);
			if (_op2_str != "")
				_modifier_list.push_back(_op2_str);
			if (_var[2] != "")
				_modifier_list.push_back(_var[2]);
			WriteValue("Database.Internal.ModifierList", _modifier_list);
		}

		//String _last_state = ReadValue<String>("Database.Internal.LastState");
		//String _current_state = ReadValue<String>("Bio.Cell.Current.State");

		if (_last_state != _current_state)
		{
			SendEvent("AND");
			int _state_depth = (int)std::count(_current_state.begin(), _current_state.end(), '/') + 1;
			WriteValue("Database.Internal.LastState", _current_state);
			WriteValue("Database.Internal.LastDepth", _state_depth);
		}
	}

	void Database::doUpdate(const Const_DB::PROC_TYPE update_type, const String& update_keyword, const String& update_statement)
	{
		LOG_D(TAG, "Database::doUpdate(%d, %s, %s)", update_type, update_keyword.c_str(), update_statement.c_str());
		WriteValue("Database.Internal.ProcType", update_type);

		//WriteValue("Database.Internal.ConditionList", Array<String>());
		//WriteValue("Database.Internal.ModifierList", Array<String>());
		//WriteValue("Database.Internal.ParameterList", Array<String>());		// it may have something added by Database.function()

		Array<String> _statement;
		_statement.push_back(update_statement + " ");

		Array<String> _model_list = ReadValue<Array<String>>("Database." + update_keyword + ".model_list");
		Set<String> _filter_list;
		for (int i = 0; i < _model_list.size(); i++)
		{
			_model_list[i] = trim(_model_list[i]);
			GetParameter(_model_list[i], _filter_list, String(1, DYNAMIC_CONDITION_PREFIX));
		}
		if (_filter_list.size() > 0)
		{
			Array<String> _model_list_orig(_model_list);
			_model_list.clear();
			_model_list.push_back("*");
			for (auto _model_name : _model_list_orig)
			{
				if (_filter_list.find(DYNAMIC_CONDITION_PREFIX + _model_name.substr(2)) == _filter_list.end())
					_model_list.push_back(_model_name);
			}
		}

		std::unordered_set<String> _table_list;
		for (auto _model_name : _model_list)
		{
			if (_model_name == "*" || std::count(_model_name.begin(), _model_name.end(), '.') >= 2)
				RetrieveTableName(_model_name, _table_list);
			else if (_model_name == "null")
				continue;
			else
			{
				ReportError(update_keyword, Error::INVALID_DATA, "Invalid model name: '" + _model_name + "'");
				return;
			}
		}
		Clone("Database.Internal.QueryClause", "Database." + update_keyword + ".subquery");
		WriteValue("Database.Internal.Columns", _model_list);
		WriteValue("Database.Internal.TableList", Array<String>(_table_list.begin(), _table_list.end()));
		WriteValue("Database.Internal.FilterList", Array<String>(_filter_list.begin(), _filter_list.end()));
		WriteValue("Database.Internal.Statement", _statement);
	}

	void Database::doRemove()
	{
		LOG_D(TAG, "Database::doRemove()");
		WriteValue("Database.Internal.ProcType", Const_DB::DELETE);

		Array<String> _model_list = ReadValue<Array<String>>("Database.Remove.model_list");
		std::unordered_set<String> _table_list;
		for (int i = 0; i < _model_list.size(); i++)
			RetrieveTableName(_model_list[i], _table_list);
		if (_table_list.size() > 1)
		{
			ReportError("Delete", Error::INVALID_DATA, "Unsupported deleting data from zero or more than one table, but there are " + std::to_string(_table_list.size()) + " tables");
			for (const auto& table : _table_list)
				LOG_E(TAG, "table name = %s", table.c_str());
			return;
		}

		Array<String> _statement;
		_statement.push_back("DELETE ");
		WriteValue("Database.Internal.Columns", _model_list);
		WriteValue("Database.Internal.TableList", Array<String>(_table_list.begin(), _table_list.end()));
		WriteValue("Database.Internal.Statement", _statement);
	}

	void Database::doError()
	{
		LOG_D(TAG, "Database::doError()");
	}

	void Database::doConfig()
	{
		LOG_D(TAG, "Database::doConfig()");
		Clone("Database.Internal.KeepAliveInterval", "Database.Config.keep_alive_interval");
		Clone("Database.Internal.ConnectionPoolSize", "Database.Config.connection_pool_size");
	}

	void Database::RetrieveTableName(const String& parameters, std::unordered_set<String>& table_list)
	{
		//if (parameters.size() > 0 && parameters.front() == '(' && parameters.find(") AS ") != String::npos)
		//	return;
		if (parameters.size() >= 2 && parameters[0] == ':' && parameters[1] == ':')
			return;
		size_t _start_pos = parameters.find('.');
		size_t _end_pos = 0;
		while (_start_pos != String::npos)
		{
			_end_pos = parameters.find_first_of(SQL_SYMBOLS, _start_pos);
			if (_end_pos == String::npos)
				_end_pos = parameters.size();
			size_t _last_dot_pos = parameters.rfind('.', _end_pos - 1);
			if (_last_dot_pos <= _start_pos)
			{
				if (_end_pos == parameters.size())
					break;
				else
				{
					_start_pos = parameters.find('.', _end_pos + 1);
					continue;
				}
			}
			else
			{
				size_t _namestart_pos = parameters.substr(0, _start_pos).find_last_of(SQL_SYMBOLS);
				if (_namestart_pos != String::npos)
				{
					String _table_name = parameters.substr(_namestart_pos + 1, _last_dot_pos - _namestart_pos - 1);
					_table_name.erase(std::remove(_table_name.begin(), _table_name.end(), '`'), _table_name.end());
					table_list.insert(_table_name);
				}
				else
				{
					String _table_name = parameters.substr(0, _last_dot_pos);
					_table_name.erase(std::remove(_table_name.begin(), _table_name.end(), '`'), _table_name.end());
					table_list.insert(_table_name);
				}
				if (_end_pos == parameters.size())
					break;
				else
					_start_pos = parameters.find('.', _end_pos + 1);
			}
		}

	}

	String Database::GetParameter(const String& parameter, Set<String>& target_set, const String& prefix)
	{
		String _ret = parameter;
		bool _is_model_name = false;
		size_t _start_pos = 0, _end_pos = 0;
		while ((_start_pos = _ret.find("::", _end_pos)) != String::npos)
		{
			_start_pos += 2;
			_end_pos = _ret.find_first_of(SQL_SYMBOLS, _start_pos);
			String _model_name = "";
			if (_end_pos == String::npos)
			{
				_model_name = _ret.substr(_start_pos, _ret.size() - _start_pos);
				//std::replace(_model_name.begin(), _model_name.end(), '.', '_');
				if (ReadValue<String>(_model_name) == "")
					std::replace(_ret.begin() + _start_pos, _ret.end(), '.', '_');
			}
			else
			{
				_model_name = _ret.substr(_start_pos, _end_pos - _start_pos);
				//std::replace(_model_name.begin(), _model_name.end(), '.', '_');
				if (ReadValue<String>(_model_name) == "")
					std::replace(&_ret[_start_pos], &_ret[_end_pos], '.', '_');
				_end_pos++;
			}
			target_set.insert(prefix + _model_name);
		}
		if (_end_pos > 0)
		{
			// to remove quoted parameters of functions, ex. UUID_TO_BIN('::model_name',1) which model_name must be treated as a string, but need escaping mechansim
			//_ret = std::regex_replace(_ret, std::regex("(?<![\\\\])[']"), "");	// negative lookbehind not supported by C++
			RemoveQuoteAndUnescaping(_ret);
			//_ret = std::regex_replace(_ret, std::regex(R"(\')"), "'");			// not working for unknown reason!!
			_ret = std::regex_replace(_ret, std::regex("::"), prefix);
		}
		return _ret;
	}

	void Database::Split(const String& input, Array<String>& output, const String& delemiters, const Array<Pair<String, String>>& ignored_pairs)
	{
		size_t _found_index = 0;
		size_t _start_index = 0;
		while ((_found_index = input.find_first_of(delemiters, _found_index)) != String::npos)
		{
			if (_start_index != _found_index)
			{
				String _cut_off = input.substr(_start_index, _found_index - _start_index);
				bool _ignored_pair_found = false;
				for (auto _ignored_pair : ignored_pairs)
				{
					size_t _ignored_start = input.find_first_of(_ignored_pair.first, _start_index);
					if (_ignored_start != String::npos && _ignored_start <= _found_index)
					{
						//size_t _ignored_end = input.find_first_of(_ignored_pair.second, _ignored_start + 1);
						//size_t _ignored_end = input.find_last_of(_ignored_pair.second);
						size_t _ignored_end = FindRightBracket(input, _ignored_start, _ignored_pair);
						if (_found_index < _ignored_end)
						{
							_ignored_pair_found = true;
							_found_index = _ignored_end;
						}
						break;
					}
				}
				if (_ignored_pair_found == false)
				{
					output.push_back(_cut_off);
					_start_index = input.find_first_not_of(delemiters, _found_index + 1);
					_start_index = input.find_first_not_of(" ", _start_index);
					_found_index = _start_index;
				}
			}
			else
			{
				_start_index = input.find_first_not_of(delemiters, _found_index + 1);
				_found_index = _start_index;
			}
		}
		if (_start_index != String::npos)
			output.push_back(input.substr(_start_index));
	}

	String Database::RemoveAlias(const String& var)
	{
		size_t _last_alias = var.rfind(" AS ");
		if (_last_alias != String::npos)
			return var.substr(0, _last_alias);
		else
			return var;
	}

	void Database::ReportError(const String& name, Error::ERROR_CODE code, const String& message)
	{
		WriteValue("Database.Error.code", code);
		WriteValue("Database.Error.msg", message);
		String _error_code_name = Error::descriptor()->FindFieldByName("code")->enum_type()->FindValueByNumber((int)code)->name();
		LOG_E(TAG, "ERROR! code: %s, message: %s", _error_code_name.c_str(), message.c_str());
		SendEvent("Database.Error." + name);
	}

	size_t Database::FindRightBracket(const String& target, size_t pos, const Pair<String, String>& bracket_pair)
	{
		size_t _ret = pos;
		int _left_bracketcount = 1;
		int _right_bracket_count = 0;
		while (_right_bracket_count < _left_bracketcount)
		{
			_ret = target.find_first_of(bracket_pair.second, _ret + 1);
			if (_ret < target.size())
			{
				_left_bracketcount = (int)std::count(target.begin() + pos, target.begin() + _ret + 1, bracket_pair.first[0]);
				_right_bracket_count = (int)std::count(target.begin() + pos, target.begin() + _ret + 1, bracket_pair.second[0]);
			}
			else
			{
				LOG_W(TAG, "Brackets are not paired for %s", target.c_str());
				break;
			}
		}
		return _ret;
	}
	int Database::GetAncestorDepth(const String& parent1, const String& parent2)
	{
		if (parent1 == "" || parent2 == "")
			return 0;
		String _ancestor1 = parent1, _ancestor2 = parent2;
		int _ancestor_depth1 = (int)std::count(_ancestor1.begin(), _ancestor1.end(), '/') + 1;
		int _ancestor_depth2 = (int)std::count(_ancestor2.begin(), _ancestor2.end(), '/') + 1;
		while (_ancestor1 != _ancestor2)
		{
			if (_ancestor_depth1 > _ancestor_depth2)
			{
				_ancestor1 = _ancestor1.substr(0, _ancestor1.rfind('/'));
				_ancestor_depth1 = (int)std::count(_ancestor1.begin(), _ancestor1.end(), '/') + 1;
			}
			else if (_ancestor_depth1 < _ancestor_depth2)
			{
				_ancestor2 = _ancestor2.substr(0, _ancestor2.rfind('/'));
				_ancestor_depth2 = (int)std::count(_ancestor2.begin(), _ancestor2.end(), '/') + 1;
			}
			else
			{
				_ancestor1 = _ancestor1.substr(0, _ancestor1.rfind('/'));
				_ancestor2 = _ancestor2.substr(0, _ancestor2.rfind('/'));
				_ancestor_depth1 = (int)std::count(_ancestor1.begin(), _ancestor1.end(), '/') + 1;
				_ancestor_depth2 = (int)std::count(_ancestor2.begin(), _ancestor2.end(), '/') + 1;
			}
		}
		return _ancestor_depth1;
	}
	void Database::RemoveQuoteAndUnescaping(String& src)
	{
		int i = 0, j = 0;
		for (; i < src.size(); i++)
		{
			if (src[i] == '\'')
			{
				if (i > 0)
				{
					if (src[i - 1] == '\\')
						src[j - 1] = src[i];
				}
			}
			else
			{
				if (i > j)
					src[j] = src[i];
				j++;
			}
		}
		if (i > j)
		{
			src.resize(j);
			src.shrink_to_fit();
		}
	}
}
