#include "SQLServer.h"
#include <codecvt>
#include <locale>

namespace Database
{
#ifndef STATIC_API
	extern "C" PUBLIC_API BioSys::RNA* CreateInstance(BioSys::IBiomolecule* owner)
	{
		return new SQLServer(owner);
	}
#endif

	Map<String, ProcedureInfo> SQLServer::proccall_info_map_;
	Mutex SQLServer::proccall_info_mutex_;

	const Map<String, Array<String>> format_map::datetime =
	{
		{"YYYY",	{"%Y",	"yyyy",	"yyyy"}},
		//{"YY",	{"%y",	"yy",	"yy"}},
		//{"MMMM",	{"%M",	"MMMM",	"Month"}},
		//{"MMM",	{"%b",	"MMM",	"Mon"}},
		{"MM",		{"%m",	"MM",	"mm"}},
		{"DD",		{"%d",	"dd",	"dd"}},
		//{"D",		{"%e",	"d",	"dd"}},
		{"hh",		{"%H",	"HH",	"hh24"}},
		//{"HH",	{"%H",	"HH",	"hh24"}},
		//{"H",		{"%k",	"H",	"hh24"}},
		//{"hh",	{"%h",	"hh",	"hh"}},
		//{"h",		{"%l",	"h",	"hh"}},
		{"mm",		{"%i",	"mm",	"mi"}},
		{"ss",		{"%s",	"ss",	"ss"}},
		//{"tt",	{"%p",	"tt",	"am"}},
		//{"dddd",	{"%W",	"dddd",	"day"}},
		//{"ddd",	{"%a",	"ddd",	"dy"}},
		{"ffffff",	{"%f",	"ffffff","ff"}},
	};
	const Map<String, Array<String>> format_map::keyword =
	{
		{" PAGING ",	{" OFFSET "," OFFSET ",	" OFFSET "}},
		{" SIZE ",		{" LIMIT ",	" FETCH ",	" FETCH "}},
	};
	const String SQLServer::MAX_VCHAR_PARAM_SIZE = "2048";
	const String SQLServer::MAX_BINARY_PARAM_SIZE = "255";
	const String SQLServer::MAX_MODEL_NAME_SIZE = "128";
	const Set<String> SQLServer::_RESERVED_KEYWORD = { "","current_time" };
	const String SQLServer::PROC_KEYWORDS[] = { "Database.Internal.Statement",
		"Database.Internal.Columns","Database.Internal.TableList","Database.Internal.ConditionList","Database.Internal.GroupBy",
		"Database.Internal.OrderBy","Database.Internal.FilterList","Database.Internal.ParameterList","Database.Internal.ModifierList",
		"Database.Internal.QueryClause","Database.Internal.Conjunction","Database.Internal.LastDepth","Database.Internal.LastState",
		"Database.Internal.ProcType","Database.Internal.CommitContent"
	};
	const String SQLServer::CLAUSE_KEYWORDS[] = { "Database.Internal.Statement",
		"Database.Internal.Columns","Database.Internal.TableList","Database.Internal.ConditionList","Database.Internal.GroupBy",
		"Database.Internal.OrderBy","Database.Internal.FilterList","Database.Internal.ParameterList","Database.Internal.ModifierList",
		"Database.Internal.QueryClause","Database.Internal.Conjunction","Database.Internal.LastDepth","Database.Internal.LastState",
		"Database.Internal.ProcType","Database.Internal.CommitContent"
	};
	const Map<String, String> SQLServer::SQL_KEYWORD =
	{
		{"SELECT ",		"Select "},
		{" FROM ",		" From "},
		{" WHERE ",		" Where "}
	};
	const Set<String> SQLServer::NON_QUOTED_KEYWORD =
	{
		"null",
		"true",
		"false",
		"TRUE",
		"FALSE",
		"True",
		"False"
	};
	const String SQLServer::SQL_SYMBOLS = "()%-:'\", ;$";		// ; for internal use
	Obj<ObjectPool<ODBCAPI>> SQLServer::db_connection_pool_ = nullptr;
	TimerQueue SQLServer::timer_queue_;

	SQLServer::SQLServer(BioSys::IBiomolecule* owner)
		:RNA(owner, "SQLServer", this)
	{
		init();
	}

	SQLServer::~SQLServer()
	{
	}

	void SQLServer::OnEvent(const DynaArray& name)
	{
		USING_BIO_NAMESPACE
			String _name = name.str();
		switch (hash(_name))
		{
		case "Database.CreateStoredProc"_hash:
		{
			doCreateStoredProc();
			break;
		}
		case "Database.CallStoredProc"_hash:
		{
			doCallStoredProc();
			break;
		}
		case "Database.Function"_hash:
		{
			doFunction();
			break;
		}
		case "Database.Setup"_hash:
		{
			doSetup();
			break;
		}
		case "Database.CreateClause"_hash:
		{
			doCreateClause();
			break;
		}
		case "Database.WaitForCall"_hash:
		{
			String _event_name = ReadValue<String>(_name + ".name");
			procedure_call_queue_.push({ _event_name, ReadValue<String>((String)"encode." + _event_name), ReadValue<unsigned long long>((String)"src_instance." + _event_name) });
			LOG_I(TAG, "[%p] WaitForCall(%s) queue size=%zd", this, _event_name.c_str(), procedure_call_queue_.size());
			break;
		}
		case "Database.Execute"_hash:
		{
			doExecute();
			break;
		}
		default:
			break;
		}
	}

	void SQLServer::doCreateStoredProc()
	{
		//if (db_ == nullptr)
		//{
		//	ReportError(Error::DATABASE_NOT_FOUND, "Database connection has not been initilized! name: " + _create_stored_proc.name());
		//	return;
		//}
		WriteValue("Database.Internal.CommitContent", "");
		ConstAPI::PROC_TYPE _proc_type = (ConstAPI::PROC_TYPE)ReadValue<int>("Database.Internal.ProcType");

		String _name = ReadValue<String>("Database.CreateStoredProc.name");
		String _database_name = ReadValue<String>("SQLServer.Internal.Name");
		LOG_D(TAG, "##### doCreateStoredProc(%s::%s) ####", _database_name.c_str(), _name.c_str());
		String _proc_full_name = "";
		if (_name == "")
		{
			_name = ReadValue<String>("Database.Internal.Title");
			_proc_full_name = _database_name + "." + _name;
		}
		else if (_name.substr(0, _database_name.size()) != _database_name)
		{
			_proc_full_name = _database_name + "." + _name;
		}
		else
		{
			_proc_full_name = _name;
			_name = _name.substr(_database_name.size() + 1);
		}

		{
			MutexLocker proccall_info_lock_(proccall_info_mutex_);
			auto _itr = proccall_info_map_.find(_proc_full_name);
			if (_itr != proccall_info_map_.end())
			{
				ProcedureInfo& _procedure_info = _itr->second;
				if (_procedure_info.proceed_items.size() != 0)
				{
					LOG_W(TAG, "Create a stored procedure with the same name %s", _proc_full_name.c_str());
					//assert(false);
					ResetAll();
					Done(_proc_full_name, ReadValue<String>("Database.CreateStoredProc.done_message"));
					return;
				}
			}
		}

		String _output = "CREATE PROCEDURE dbo." + _name + "\r\n";
		switch (_proc_type)
		{
		case ConstAPI::QUERY:
		{
			Array<String> _statement;
			Array<String> _inner_statement;
			Array<String> _param_list = ReadValue<Array<String>>("Database.Internal.ParameterList");
			if (CreateSelectStatement(_inner_statement, _param_list) == false)
				return;

			ConvertToDynamicFormat(_inner_statement, _param_list);

			//_inner_statement.push_back(";\r\n");
			bool _inside_transaction = ReadValue<bool>("Database.CreateStoredProc.rollback_if_error");
			if (_inside_transaction)
			{
				_statement.push_back(
					" BEGIN TRY\r\n"
					"  BEGIN TRANSACTION\r\n");
			}

			for (auto _batch_item : ReadValue<Array<String>>("Database.CreateStoredProc.batch_list"))
			{
				String _clause = RemoveAlias(ReadValue<String>(_batch_item));
				if (_clause.size() < 2)
				{
					ReportError(_proc_full_name, Error::INVALID_DATA, "No such clause: " + _batch_item, ReadValue<String>("Database.CreateStoredProc.err_message"));
					return;
				}
				if (_clause.front() == '(' && _clause.back() == ')')
					_clause = _clause.substr(1, _clause.size() - 2);
				_statement.push_back(_clause);
				_statement.push_back(";\r\n");
			}

			_statement.insert(_statement.end(), std::make_move_iterator(_inner_statement.begin()), std::make_move_iterator(_inner_statement.end()));

			_statement.push_back(";\r\n");

			if (_inside_transaction)
			{
				_statement.push_back(
					"  COMMIT TRANSACTION\r\n"
					"END TRY\r\n"
					"BEGIN CATCH\r\n"
					"  IF @@TRANCOUNT > 0 ROLLBACK TRAN\r\n"
					"  DECLARE @ErrorMessage NVARCHAR(4000) = ERROR_MESSAGE()\r\n"
					"  DECLARE @ErrorSeverity INT = ERROR_SEVERITY()\r\n"
					"  DECLARE @ErrorState INT = ERROR_STATE()\r\n"
					"--Use RAISERROR inside the CATCH block to return error\r\n"
					"-- information about the original error that caused\r\n"
					"-- execution to jump to the CATCH block.\r\n"
					"  RAISERROR(@ErrorMessage, @ErrorSeverity, @ErrorState);\r\n"
					"END CATCH\r\n"
				);
			}
			{
				MutexLocker proccall_info_lock_(proccall_info_mutex_);
				auto _itr = proccall_info_map_.find(_proc_full_name);
				if (_itr != proccall_info_map_.end())
				{
					ProcedureInfo& _procedure_info = _itr->second;
					Array<String> _tmp_param_list = _procedure_info.param_list;
					std::swap(_tmp_param_list, _param_list);
					for (unsigned int i = 0; i < _param_list.size(); i++)
						std::replace(_param_list[i].begin(), _param_list[i].end(), '.', '_');
					for (auto _param : _tmp_param_list)
					{
						if (std::find(_param_list.begin(), _param_list.end(), _param) == _param_list.end())
							_param_list.push_back(_param);
					}
				}
			}

			for (unsigned int i = 0; i < _param_list.size(); i++)
			{
				String _type_str;
				switch (param_type_[_param_list[i]])
				{
				case INT:
					_type_str = " INT";
					break;
				case BINARY:
					_type_str = " VARBINARY(" + MAX_BINARY_PARAM_SIZE + ")";
					break;
				default:
					_type_str = " NVARCHAR(MAX)";		// instead of TEXT
					break;
				}
				_output += "@" + _param_list[i] + _type_str;
				if (i < _param_list.size() - 1)
					_output += ", ";
			}
			_output += "\r\nAS\r\nBEGIN\r\n";

			if (ReadValue<bool>("Database.CreateStoredProc.disable_only_full_group_by") == true)
			{
				_output += "SET sql_mode=(SELECT REPLACE(@@sql_mode,'ONLY_FULL_GROUP_BY',''));\r\n";
			}

			for (unsigned int i = 0; i < _statement.size(); i++)
				_output += _statement[i];
			_output += "END";
			WriteValue("Database.Internal.CommitContent", _output);

			if (ReadValue<bool>("Database.CreateStoredProc.show_result") == true)
			{
				LOG_I(TAG, "[%p] doCreateStoredProc() statement: %s", this, ReadValue<String>("Database.Internal.CommitContent").c_str());
			}
			try
			{
				{
					if (db_connection_pool_ == nullptr)
					{
						ReportError(_proc_full_name, Error::DATABASE_NOT_SETUP_YET, "Please \"Setup\" database connection first", ReadValue<String>("Database.CreateStoredProc.err_message"));
						return;
					}
					Obj<ODBCAPI> _db = db_connection_pool_->borrow();
					_db->execute("DROP PROCEDURE IF EXISTS " + _name + ";");
					_db->execute(ReadValue<String>("Database.Internal.CommitContent"));
				}
				{
					MutexLocker proccall_info_lock_(proccall_info_mutex_);
					auto _itr = proccall_info_map_.find(_proc_full_name);
					if (_itr == proccall_info_map_.end())
					{
						proccall_info_map_.insert(std::make_pair(_proc_full_name, ProcedureInfo()));
						_itr = proccall_info_map_.find(_proc_full_name);
					}
					ProcedureInfo& _procedure_info = _itr->second;
					_procedure_info.name = _proc_full_name;
					_procedure_info.type = _proc_type;
					Array<String> _list = ReadValue<Array<String>>("Database.Internal.Columns");
					_procedure_info.proceed_items.insert(_procedure_info.proceed_items.end(), _list.begin(), _list.end());
					_list = ReadValue<Array<String>>("Database.Internal.TableList");
					//for (auto _column : column_list_)
					//{
					//	_procedure_info.column_list.insert({ _column.first , _column.second });
					//}
					_procedure_info.column_list.insert(column_list_.begin(), column_list_.end());
					_list = ReadValue<Array<String>>("Database.Internal.ParameterList");
					for (auto param : _list)
					{
						if (std::find(_procedure_info.param_list.begin(), _procedure_info.param_list.end(), param) == _procedure_info.param_list.end())
							_procedure_info.param_list.push_back(param);
					}
					_list = ReadValue<Array<String>>("Database.Internal.FilterList");
					_procedure_info.filter_list.insert(_procedure_info.filter_list.end(), _list.begin(), _list.end());
				}

				ResetAll();

				Done(_proc_full_name, ReadValue<String>("Database.CreateStoredProc.done_message"));
			}
			catch (const std::exception& e)
			{
				LOG_E(TAG, "[%p] doCreateStoredProc() Execption: %s", this, e.what());
				ReportError(_proc_full_name, Error::CREATE_PROC_ERROR, e.what(), ReadValue<String>("Database.CreateStoredProc.err_message"));
			}
			break;
		}
		default:
		{
			Array<String> _statement;
			Array<String> _inner_statement;
			Array<String> _param_list = ReadValue<Array<String>>("Database.Internal.ParameterList");
			switch (_proc_type)
			{
			case ConstAPI::INSERT:
				if (CreateInsertStatement(_inner_statement, _param_list) == false)
					return;
				for (unsigned int i = 0; i < _param_list.size(); i++)
				{
					auto _itr = column_list_.end();
					if ((_itr = column_list_.find(_param_list[i])) != column_list_.end())
					{
						std::replace(_param_list[i].begin(), _param_list[i].end(), '.', '_');
						//if (_itr->second.data_type_name_.find("binary") != String::npos)
						//	param_type_[_param_list[i]] = BINARY;
						//else if (_itr->second.data_type_name_.find("int") != String::npos)
						//	param_type_[_param_list[i]] = INT;
					}
					else
					{
						std::replace(_param_list[i].begin(), _param_list[i].end(), '.', '_');
					}
				}
				ConvertToDynamicFormat(_inner_statement, _param_list);
				break;
			case ConstAPI::UPDATE:
				if (CreateUpdateStatement(_inner_statement, _param_list) == false)
					return;
				for (unsigned int i = 0; i < _param_list.size(); i++)
				{
					auto _itr = column_list_.end();
					if ((_itr = column_list_.find(_param_list[i])) != column_list_.end())
					{
						std::replace(_param_list[i].begin(), _param_list[i].end(), '.', '_');
						//if (_itr->second.data_type_name_.find("binary") != String::npos)
						//	param_type_[_param_list[i]] = BINARY;
						//else if (_itr->second.data_type_name_.find("int") != String::npos)
						//	param_type_[_param_list[i]] = INT;
					}
					else
					{
						std::replace(_param_list[i].begin(), _param_list[i].end(), '.', '_');
					}
				}
				break;
			case ConstAPI::DELETE:
			{
				if (CreateDeleteStatement(_inner_statement, _param_list) == false)
					return;
				for (unsigned int i = 0; i < _param_list.size(); i++)
				{
					auto _itr = column_list_.end();
					if ((_itr = column_list_.find(_param_list[i])) != column_list_.end())
					{
						std::replace(_param_list[i].begin(), _param_list[i].end(), '.', '_');
						//if (_itr->second.data_type_name_.find("binary") != String::npos)
						//	param_type_[_param_list[i]] = BINARY;
						//else if (_itr->second.data_type_name_.find("int") != String::npos)
						//	param_type_[_param_list[i]] = INT;
					}
					else
					{
						std::replace(_param_list[i].begin(), _param_list[i].end(), '.', '_');
					}
				}
				ConvertToDynamicFormat(_inner_statement, _param_list);
				break;
			}
			default:
				assert(false);
				return;
			}
			bool _inside_transaction = ReadValue<bool>("Database.CreateStoredProc.rollback_if_error");
			if (_inside_transaction)
			{
				_statement.push_back(
					"BEGIN TRY\r\n"
					"  BEGIN TRANSACTION\r\n");
			}

			for (auto _batch_item : ReadValue<Array<String>>("Database.CreateStoredProc.batch_list"))
			{
				String _clause = RemoveAlias(ReadValue<String>(_batch_item));
				if (_clause.size() < 2)
				{
					ReportError(_proc_full_name, Error::INVALID_DATA, "No such clause: " + _batch_item, ReadValue<String>("Database.CreateStoredProc.err_message"));
					return;
				}
				if (_clause.front() == '(' && _clause.back() == ')')
					_clause = _clause.substr(1, _clause.size() - 2);
				_statement.push_back(_clause);
				_statement.push_back(";\r\n");
			}

			_statement.insert(_statement.end(), std::make_move_iterator(_inner_statement.begin()), std::make_move_iterator(_inner_statement.end()));

			_statement.push_back(";\r\n");

			if (_inside_transaction)
			{
				_statement.push_back(
					"  COMMIT TRANSACTION\r\n"
					"END TRY\r\n"
					"BEGIN CATCH\r\n"
					"  IF @@TRANCOUNT > 0 ROLLBACK TRAN\r\n"
					"  DECLARE @ErrorMessage NVARCHAR(4000) = ERROR_MESSAGE()\r\n"
					"  DECLARE @ErrorSeverity INT = ERROR_SEVERITY()\r\n"
					"  DECLARE @ErrorState INT = ERROR_STATE()\r\n"
					"--Use RAISERROR inside the CATCH block to return error\r\n"
					"-- information about the original error that caused\r\n"
					"-- execution to jump to the CATCH block.\r\n"
					"  RAISERROR(@ErrorMessage, @ErrorSeverity, @ErrorState);\r\n"
					"END CATCH\r\n"
				);
			}
			{
				MutexLocker proccall_info_lock_(proccall_info_mutex_);
				auto _itr = proccall_info_map_.find(_proc_full_name);
				if (_itr != proccall_info_map_.end())
				{
					ProcedureInfo& _procedure_info = _itr->second;
					Array<String> _tmp_param_list = _procedure_info.param_list;
					std::swap(_tmp_param_list, _param_list);
					for (unsigned int i = 0; i < _param_list.size(); i++)
						std::replace(_param_list[i].begin(), _param_list[i].end(), '.', '_');
					for (auto _param : _tmp_param_list)
					{
						if (std::find(_param_list.begin(), _param_list.end(), _param) == _param_list.end())
							_param_list.push_back(_param);
					}
				}
			}

			for (unsigned int i = 0; i < _param_list.size(); i++)
			{
				String _type_str;
				switch (param_type_[_param_list[i]])
				{
				case INT:
					_type_str = " INT";
					break;
				case BINARY:
					_type_str = " VARBINARY(" + MAX_BINARY_PARAM_SIZE + ")";
					break;
				default:
					_type_str = " NVARCHAR(MAX)";		// instead of TEXT
					break;
				}
				_output += "@" + _param_list[i] + _type_str;
				if (i < _param_list.size() - 1)
					_output += ", ";
			}
			_output += "\r\nAS\r\nBEGIN\r\n";

			for (unsigned int i = 0; i < _statement.size(); i++)
				_output += _statement[i];
			_output += "END";
			WriteValue("Database.Internal.CommitContent", _output);

			if (ReadValue<bool>("Database.CreateStoredProc.show_result") == true)
			{
				LOG_I(TAG, "[%p] doCreateStoredProc() statement: %s", this, ReadValue<String>("Database.Internal.CommitContent").c_str());
			}
			try
			{
				{
					if (db_connection_pool_ == nullptr)
					{
						ReportError(_proc_full_name, Error::DATABASE_NOT_SETUP_YET, "Please \"Setup\" database connection first", ReadValue<String>("Database.CreateStoredProc.err_message"));
						return;
					}
					Obj<ODBCAPI> _db = db_connection_pool_->borrow();
					_db->execute("DROP PROCEDURE IF EXISTS " + _name + ";");
					_db->execute(ReadValue<String>("Database.Internal.CommitContent"));
				}
				{
					MutexLocker proccall_info_lock_(proccall_info_mutex_);
					auto _itr = proccall_info_map_.find(_proc_full_name);
					if (_itr == proccall_info_map_.end())
					{
						proccall_info_map_.insert(std::make_pair(_proc_full_name, ProcedureInfo()));
						_itr = proccall_info_map_.find(_proc_full_name);
					}
					ProcedureInfo& _procedure_info = _itr->second;
					_procedure_info.name = _proc_full_name;
					_procedure_info.type = _proc_type;
					//Array<String>& _list = ReadValue<Array<String>>("Database.Internal.Columns");
					//_procedure_info.proceed_items.insert(_procedure_info.proceed_items.end(), _list.begin(), _list.end());
					//_list = ReadValue<Array<String>>("Database.Internal.TableList");
					//_procedure_info.table_list.insert(_procedure_info.table_list.end(), _list.begin(), _list.end());
					for (auto _column : column_list_)
					{
						//String _column_name = _column.first;
						//int _column_index = _column.second.index_;
						//_column_name.erase(std::remove(_column_name.begin(), _column_name.end(), '`'), _column_name.end());
						//_procedure_info.column_list.insert({ _column_name , _column_index });
						_procedure_info.column_list.insert({ _column.first , _column.second });
					}
					//_procedure_info.column_list.insert(column_list_.begin(), column_list_.end());
					Array<String> _list = ReadValue<Array<String>>("Database.Internal.ParameterList");
					for (auto param : _list)
					{
						if (std::find(_procedure_info.param_list.begin(), _procedure_info.param_list.end(), param) == _procedure_info.param_list.end())
							_procedure_info.param_list.push_back(param);
					}
					_list = ReadValue<Array<String>>("Database.Internal.FilterList");
					_procedure_info.filter_list.insert(_procedure_info.filter_list.end(), _list.begin(), _list.end());
				}

				ResetAll();

				Done(_proc_full_name, ReadValue<String>("Database.CreateStoredProc.done_message"));
			}
			catch (const std::exception& e)
			{
				LOG_E(TAG, "[%p] doCreateStoredProc() Execption: %s", this, e.what());
				ReportError(_proc_full_name, Error::CREATE_PROC_ERROR, e.what(), ReadValue<String>("Database.CreateStoredProc.err_message"));
				return;
			}
			break;
		}
		}
	}

	void SQLServer::doCallStoredProc()
	{
		String _database_name = ReadValue<String>("SQLServer.Internal.Name");
		if (_database_name != database_name_)
			return;
		String _proc_name = ReadValue<String>("Database.CallStoredProc.name");
		LOG_D(TAG, "##### doCallStoredProc(%s::%s) ####", _database_name.c_str(), _proc_name.c_str());
		if (_proc_name.substr(0, _database_name.size()) != _database_name)
			_proc_name = _database_name + "." + _proc_name;

		ProcedureInfo _stored_proc_info;
		{
			MutexLocker proccall_info_lock_(proccall_info_mutex_);
			auto itr = proccall_info_map_.find(_proc_name);
			if (itr == proccall_info_map_.end())
			{
				ReportError(_proc_name, Error::PROC_NOT_FOUND, "The stored procedure has not been initilized! name: " + _proc_name, ReadValue<String>("Database.CallStoredProc.err_message"));
				return;
			}
			else
			{
				_stored_proc_info = itr->second;
			}
		}
		String _short_proc_name = std::regex_replace(_proc_name, std::regex(_database_name), "dbo");
		String _statement = "Exec " + _short_proc_name + " ";
		try
		{
			switch (_stored_proc_info.type)
			{
			case ConstAPI::QUERY:
			{
				const Map<String, SchemaInfo>& _column_list = _stored_proc_info.column_list;
				const Array<String>& _param_list = _stored_proc_info.param_list;
				unsigned int i = 0;
				try
				{
					bool _perfrom_dynamic_condition = false;
					for (auto _elem : _param_list)
					{
						if (_elem[0] == DYNAMIC_CONDITION_PREFIX)
						{
							_perfrom_dynamic_condition = true;
							break;
						}
					}
					for (i = 0; i < _param_list.size(); i++)
					{
						bool _is_dynamic_condition_item = false;
						String _elem = _param_list[i];
						if (_elem[0] == DYNAMIC_CONDITION_PREFIX)
						{
							_is_dynamic_condition_item = true;
							_elem = _elem.substr(1);
						}
						String _data_type = GetColumnType(_column_list, _elem);
						String _data_in_model = ReadValue<String>(_elem);
						if ((_data_in_model == "" || _data_in_model == "''" || _data_in_model == "\"\"") && _data_type.find("binary") != String::npos)
						{
							_data_in_model = "null";
						}
						String _param = _param_list[i];
						std::replace(_param.begin(), _param.end(), '.', '_');
						if (_data_in_model == _elem)
						{
							ReportError(_proc_name, Error::INVALID_DATA, "Invalid data in model: '" + _elem + "'", ReadValue<String>("Database.CallStoredProc.err_message"));
							return;
						}
						else if (modelname_function_map_.find(_data_in_model) != modelname_function_map_.end())
						{	// function type
							String _quote_string = _perfrom_dynamic_condition ? "'" : "";
							std::replace(_data_in_model.begin(), _data_in_model.end(), ';', ',');
							if (i < _param_list.size() - 1)
								_statement += "@" + _param + "=" + _quote_string + RemoveAlias(_data_in_model) + _quote_string + ", ";
							else
								_statement += "@" + _param + "=" + _quote_string + RemoveAlias(_data_in_model) + _quote_string;
						}
						else
						{
							bool _is_keyword = NON_QUOTED_KEYWORD.count(_data_in_model);
							String _quote_string = (_perfrom_dynamic_condition || _is_keyword == 0) ? "'" : "";
							//std::replace(_data_in_model.begin(), _data_in_model.end(), ';', ',');
							if (_data_in_model.size() >= 2 && _data_in_model.front() == '\"' && _data_in_model.back() == '\"')
								_data_in_model = _data_in_model.substr(1, _data_in_model.size() - 2);
							_data_in_model = std::regex_replace(_data_in_model, std::regex("'"), "\\'");

							if (i < _param_list.size() - 1)
								_statement += "@" + _param + "=" + _quote_string + (_perfrom_dynamic_condition && !_is_dynamic_condition_item && !_is_keyword ? "\\'" : "") + _data_in_model + (_perfrom_dynamic_condition && !_is_dynamic_condition_item && !_is_keyword ? "\\'" : "") + _quote_string + ", ";
							else
								_statement += "@" + _param + "=" + _quote_string + (_perfrom_dynamic_condition && !_is_dynamic_condition_item && !_is_keyword ? "\\'" : "") + _data_in_model + (_perfrom_dynamic_condition && !_is_dynamic_condition_item && !_is_keyword ? "\\'" : "") + _quote_string;
						}
					}
				}
				catch (const std::exception&)
				{
					ReportError(_proc_name, Error::INVALID_DATA, "Invalid data in model: '" + _param_list[i] + "'", ReadValue<String>("Database.CallStoredProc.err_message"));
					return;
				}

				Array<int> _index_mapping;
				Array<String> _query_items = _stored_proc_info.proceed_items;
				const Array<String>& _filter_list = _stored_proc_info.filter_list;
				if (_query_items.size() >= 1 && _query_items[0] == "*" && _filter_list.size() > 0)
				{
					//_query_items.clear();
					_query_items.erase(_query_items.begin());
					for (unsigned int i = 0; i < _query_items.size(); i++)
						_index_mapping.push_back((int)_column_list.size() + i);

					for (auto _filter : _filter_list)
					{
						if (_filter[0] == DYNAMIC_CONDITION_PREFIX)
							_filter = _filter.substr(1);
						Array<String> _model_list = ReadValue<Array<String>>(_filter);
						for (size_t i = 0; i < _model_list.size(); i++)
						{
							if (_column_list.find(_model_list[i]) != _column_list.end())
							{
								_query_items.push_back(_model_list[i]);
								_index_mapping.push_back(_column_list.at(_model_list[i]).index_);
							}
							else
							{
								ReportError(_proc_name, Error::INVALID_DATA, "Invalid model name: '" + _model_list[i] + "'", ReadValue<String>("Database.CallStoredProc.err_message"));
								return;
							}
						}
					}
				}
				else
				{
					for (unsigned int i = 0; i < _query_items.size(); i++)
						_index_mapping.push_back(i);
				}
				assert(_index_mapping.size() == _query_items.size());

				if (ReadValue<bool>("Database.CallStoredProc.show_result") == true)
				{
					LOG_I(TAG, "[%p] doCallStoredProc() statement: %s", this, _statement.c_str());
				}

				nanodbc::result _result;
				{
					if (db_connection_pool_ == nullptr)
					{
						ReportError(_proc_name, Error::DATABASE_NOT_SETUP_YET, "Please \"Setup\" database connection first", ReadValue<String>("Database.CallStoredProc.err_message"));
						return;
					}
					Obj<ODBCAPI> _db = db_connection_pool_->borrow();
					_result = _db->execute(_statement, MAX_QUERY_RESULT_COUNT);
				}
				assert(_result.columns() == _query_items.size());

				WriteValue(_proc_name + ".RowCount", (int)_result.affected_rows());
				WriteValue(_proc_name + ".ColumnCount", (unsigned long long)_query_items.size());
				WriteValue(_proc_name + ".Columns", _query_items);

				String _callback_message_name = ReadValue<String>("Database.CallStoredProc.callback_message");
				Remove(_callback_message_name + ".*");		// to prevent data interference

				Array<String> _columns_model_list;
				//for (auto _name : _query_items)
				//	_columns_model_list.push_back((String)"::" + _name);
				//WriteValue(_callback_message_name + "." + TAG_COLUMNS_SOURCE_MODEL_LIST, _columns_model_list);
				for (int i = 0; i < _query_items.size(); i++)
					_columns_model_list.push_back((String)"::" + _callback_message_name + ".column_" + std::to_string(i));
				//WriteValue(_callback_message_name + "." + TAG_COLUMNS_SOURCE_MODEL_LIST, _columns_model_list);

				int _record_count = 0;
				nanodbc::string const null_value = NANODBC_TEXT("null");
				while (_result.next())
				{
					//LOG_D(TAG, "**********  Record %d  **********", _record_count);
					_record_count++;
					WriteValue(_callback_message_name + "." + TAG_COLUMNS_SOURCE_MODEL_LIST, _columns_model_list);
					for (unsigned int i = 0; i < _query_items.size(); i++)
					{
						String _column_name = _callback_message_name + ".column_" + std::to_string(i);
						try
						{
							WriteValue(_column_name, _result.get<nanodbc::string>(_index_mapping[i], null_value));
						}
						catch (...)
						{
							WriteValue(_column_name, null_value);
						}
						//LOG_D(TAG, "column(\"%s\") = %s", _column_name.c_str(), ReadValue<String>(_column_name).c_str());
					}
					//RaiseEvent(_callback_message_name);
					SendEvent(_callback_message_name);
					//Remove(_callback_message_name + ".*");		// to prevent data interference
				}

				//Remove(_callback_message_name + "." + TAG_COLUMNS_SOURCE_MODEL_LIST);

				break;
			}
			case ConstAPI::INSERT:
			case ConstAPI::UPDATE:
			{
				const Array<String>& _filter_list = _stored_proc_info.filter_list;
				const Map<String, SchemaInfo>& _column_list = _stored_proc_info.column_list;
				const Array<String>& _param_list = _stored_proc_info.param_list;
				Set<String> _model_set;
				for (auto _filter : _filter_list)
				{
					if (_filter[0] == DYNAMIC_CONDITION_PREFIX)
						_filter = _filter.substr(1);
					Array<String> _model_list = ReadValue<Array<String>>(_filter);
					for (auto _model_name : _model_list)
					{
						if (_column_list.find(_model_name) != _column_list.end())
						{
							_model_set.insert(_model_name);
						}
						else
						{
							ReportError(_proc_name, Error::INVALID_DATA, "Invalid model name: '" + _model_name + "'", ReadValue<String>("Database.CallStoredProc.err_message"));
							return;
						}
					}
				}
				unsigned int i = 0;
				try
				{
					for (i = 0; i < _param_list.size(); i++)
					{
						String _elem = _param_list[i];
						if (_elem[0] == DYNAMIC_CONDITION_PREFIX)
							_elem = _elem.substr(1);

						//String _data_in_model_utf8 = ReadValue<String>(_elem);
						//Obj<TranscodeFromStr> _transcodeFromStr = Obj<TranscodeFromStr>(new TranscodeFromStr((const XMLByte*)_data_in_model_utf8.c_str(), _data_in_model_utf8.size(), "UTF-8"));
						//XMLCh* xmlch_ptr = _transcodeFromStr->adopt();
						//String _data_in_model = xercesc::XMLString::transcode(xmlch_ptr);
						String _data_type = GetColumnType(_column_list, _elem);
						String _data_in_model = ReadValue<String>(_elem);
						String _data_to_write = RemoveAllAlias(_data_in_model);
						if ((_data_in_model == "" || _data_in_model == "''" || _data_in_model == "\"\"") && _data_type.find("binary") != String::npos)
						{
							_data_to_write = "null";
							_data_in_model = "null";
						}
						bool _quoted = true;
						if (_data_to_write != _data_in_model || NON_QUOTED_KEYWORD.count(_data_in_model) > 0)
							_quoted = false;

						String _param = _param_list[i];
						std::replace(_param.begin(), _param.end(), '.', '_');
						if (_model_set.size() == 0)
						{
							if (_data_to_write == _elem)
							{
								ReportError(_proc_name, Error::INVALID_DATA, "Invalid data in model: '" + _elem + "'", ReadValue<String>("Database.CallStoredProc.err_message"));
								return;
							}

							if (_data_to_write.size() >= 2 && _data_to_write.front() == '\"' && _data_to_write.back() == '\"')
								_data_to_write = _data_to_write.substr(1, _data_to_write.size() - 2);
							_data_to_write = std::regex_replace(_data_to_write, std::regex("'"), "\\'");
							if (_data_to_write.size() < 2 || (_data_to_write.front() != '[' || _data_to_write.back() != ']') && (_data_to_write.front() != '{' || _data_to_write.back() != '}'))
								_data_to_write = std::regex_replace(_data_to_write, std::regex("\\\""), "\\\\\"");		// escape char of JSON string, but ignoring JSON_TABLE

							if (_data_type == "datetime")
							{
								if (is_datetime(_data_to_write))
								{	// removing time zone
									size_t _timezone_pos = _data_to_write.find(" +");
									if (_timezone_pos != String::npos)
									{
										_data_to_write = _data_to_write.substr(0, _timezone_pos);
									}
									else
									{
										_timezone_pos = _data_to_write.find(" -");
										if (_timezone_pos != String::npos)
											_data_to_write = _data_to_write.substr(0, _timezone_pos);
									}
								}
							}

							if (_quoted)
								_statement += "@" + _param + "=" + "'" + _data_to_write + "'";
							else
							{
								std::replace(_data_to_write.begin(), _data_to_write.end(), ';', ',');
								_statement += "@" + _param + "=" + _data_to_write;
							}
						}
						else
						{
							if (_model_set.find(_elem) != _model_set.end())
							{
								if (_data_to_write == _elem)
								{
									ReportError(_proc_name, Error::INVALID_DATA, "Invalid data in model: '" + _elem + "'", ReadValue<String>("Database.CallStoredProc.err_message"));
									return;
								}

								if (_data_to_write.size() >= 2 && _data_to_write.front() == '\"' && _data_to_write.back() == '\"')
									_data_to_write = _data_to_write.substr(1, _data_to_write.size() - 2);
								_data_to_write = std::regex_replace(_data_to_write, std::regex("'"), "\\'");
								if (_data_to_write.size() < 2 || (_data_to_write.front() != '[' || _data_to_write.back() != ']') && (_data_to_write.front() != '{' || _data_to_write.back() != '}'))
									_data_to_write = std::regex_replace(_data_to_write, std::regex("\\\""), "\\\\\"");		// escape char of JSON string, but ignoring JSON_TABLE

								if (_quoted)
									_statement += "@" + _param + "=" + "'" + _data_to_write + "'";
								else
								{
									std::replace(_data_to_write.begin(), _data_to_write.end(), ';', ',');
									_statement += "@" + _param + "=" + _data_to_write;
								}
							}
							else
							{
								if (_column_list.find(_elem) != _column_list.end())
									_statement += "@" + _param + "=" + "NULL";
								else
								{

									if (_data_to_write.size() >= 2 && _data_to_write.front() == '\"' && _data_to_write.back() == '\"')
										_data_to_write = _data_to_write.substr(1, _data_to_write.size() - 2);
									_data_to_write = std::regex_replace(_data_to_write, std::regex("'"), "\\'");
									if (_data_to_write.size() < 2 || (_data_to_write.front() != '[' || _data_to_write.back() != ']') && (_data_to_write.front() != '{' || _data_to_write.back() != '}'))
										_data_to_write = std::regex_replace(_data_to_write, std::regex("\\\""), "\\\\\"");		// escape char of JSON string, but ignoring JSON_TABLE

									if (_quoted)
										_statement += "@" + _param + "=" + "'" + _data_to_write + "'";
									else
									{
										std::replace(_data_to_write.begin(), _data_to_write.end(), ';', ',');
										_statement += "@" + _param + "=" + _data_to_write;

									}
								}
							}
						}
						if (i < _param_list.size() - 1)
							_statement += ", ";
					}
				}
				catch (const std::exception&)
				{
					ReportError(_proc_name, Error::INVALID_DATA, "Invalid data in model: '" + _param_list[i] + "'", ReadValue<String>("Database.CallStoredProc.err_message"));
					return;
				}
				_statement += ";";
				if (ReadValue<bool>("Database.CallStoredProc.show_result") == true)
				{
					LOG_I(TAG, "[%p] doCallStoredProc() statement: %s", this, _statement.c_str());
				}

				nanodbc::result _result;
				{
					if (db_connection_pool_ == nullptr)
					{
						ReportError(_proc_name, Error::DATABASE_NOT_SETUP_YET, "Please \"Setup\" database connection first", ReadValue<String>("Database.CallStoredProc.err_message"));
						return;
					}
					Obj<ODBCAPI> _db = db_connection_pool_->borrow();
					_result = _db->execute(_statement);
				}
				WriteValue(_proc_name + ".RowCount", (int)_result.affected_rows());

				String _callback_message_name = ReadValue<String>("Database.CallStoredProc.callback_message");
				if (_callback_message_name != "")
				{
					Array<String> _query_items = _stored_proc_info.proceed_items;
					Array<int> _index_mapping;
					for (unsigned int i = 0; i < _query_items.size(); i++)
						_index_mapping.push_back(i);

					Remove(_callback_message_name + ".*");		// to prevent data interference

					Array<String> _columns_model_list;
					//for (auto _name : _query_items)
					//	_columns_model_list.push_back((String)"::" + _name);
					//WriteValue(_callback_message_name + "." + TAG_COLUMNS_SOURCE_MODEL_LIST, _columns_model_list);
					for (int i = 0; i < _query_items.size(); i++)
						_columns_model_list.push_back((String)"::" + _callback_message_name + ".column_" + std::to_string(i));
					//WriteValue(_callback_message_name + "." + TAG_COLUMNS_SOURCE_MODEL_LIST, _columns_model_list);

					int _record_count = 0;
					nanodbc::string const null_value = NANODBC_TEXT("null");
					while (_result.next())
					{
						//LOG_D(TAG, "**********  Record %d  **********", _record_count);
						_record_count++;
						WriteValue(_callback_message_name + "." + TAG_COLUMNS_SOURCE_MODEL_LIST, _columns_model_list);
						for (unsigned int i = 0; i < _query_items.size(); i++)
						{
							String _column_name = _callback_message_name + ".column_" + std::to_string(i);
							try
							{
								WriteValue(_column_name, _result.get<nanodbc::string>(_index_mapping[i], null_value));
							}
							catch (...)
							{
								WriteValue(_column_name, null_value);
							}
							//LOG_D(TAG, "column(\"%s\") = %s", _column_name.c_str(), ReadValue<String>(_column_name).c_str());
						}
						//RaiseEvent(_callback_message_name);
						SendEvent(_callback_message_name);
						//Remove(_callback_message_name + ".*");		// to prevent data interference
					}
				}
				break;
			}
			case ConstAPI::DELETE:
			{
				const Array<String>& _param_list = _stored_proc_info.param_list;
				unsigned int i = 0;
				try
				{
					for (i = 0; i < _param_list.size(); i++)
					{
						String _elem = _param_list[i];
						if (_elem[0] == DYNAMIC_CONDITION_PREFIX)
							_elem = _elem.substr(1);
						String _data_in_model = ReadValue<String>(_elem);
						if (_data_in_model == _elem)
						{
							ReportError(_proc_name, Error::INVALID_DATA, "Invalid data in model: '" + _elem + "'", ReadValue<String>("Database.CallStoredProc.err_message"));
							return;
						}
						//std::replace(_data_in_model.begin(), _data_in_model.end(), ';', ',');
						if (_data_in_model.size() >= 2 && _data_in_model.front() == '\"' && _data_in_model.back() == '\"')
							_data_in_model = _data_in_model.substr(1, _data_in_model.size() - 2);
						_data_in_model = std::regex_replace(_data_in_model, std::regex("'"), "\\'");

						bool _quoted = true;
						if (NON_QUOTED_KEYWORD.count(_data_in_model) > 0)
							_quoted = false;

						if (_quoted)
							_statement += "'" + _data_in_model + "'";
						else
							_statement += _data_in_model;
						if (i < _param_list.size() - 1)
							_statement += ", ";
					}
				}
				catch (const std::exception&)
				{
					ReportError(_proc_name, Error::INVALID_DATA, "Invalid data in model: '" + _param_list[i] + "'", ReadValue<String>("Database.CallStoredProc.err_message"));
					return;
				}
				_statement += ");";
				if (ReadValue<bool>("Database.CallStoredProc.show_result") == true)
				{
					LOG_I(TAG, "[%p] doCallStoredProc() statement: %s", this, _statement.c_str());
				}

				if (db_connection_pool_ == nullptr)
				{
					ReportError(_proc_name, Error::DATABASE_NOT_SETUP_YET, "Please \"Setup\" database connection first", ReadValue<String>("Database.CallStoredProc.err_message"));
					return;
				}
				Obj<ODBCAPI> _db = db_connection_pool_->borrow();
				_db->execute(_statement);

				break;
			}
			default:
				ReportError(_proc_name, Error::PROC_CALL_ERROR, "Not supported type: " + std::to_string(_stored_proc_info.type), ReadValue<String>("Database.CallStoredProc.err_message"));
				return;
			}
			Done(_proc_name, ReadValue<String>("Database.CallStoredProc.done_message"));

			//NextCall();
		}
		catch (const std::exception& e)
		{
			ReportError(_proc_name, Error::PROC_CALL_ERROR, "Procedure name: " + _proc_name + ", message: " + e.what(), ReadValue<String>("Database.CallStoredProc.err_message"));

			//NextCall();
		}
	}

	bool SQLServer::IsParameterFromModel(const String& parameter)
	{
		return (parameter[0] == ':' && parameter[1] == ':');
	}

	String SQLServer::GetParameter(const String& parameter)
	{
		String _ret = parameter;
		bool _is_model_name = IsParameterFromModel(parameter);
		if (_is_model_name)
		{
			_ret.assign(parameter.begin() + 2, parameter.end());
			Array<String> _param_list = ReadValue<Array<String>>("Database.Internal.ParameterList");
			_param_list.push_back(_ret);
			WriteValue("Database.Internal.ParameterList", _param_list);
			std::replace(_ret.begin(), _ret.end(), '.', '_');
		}
		return _ret;
	}

	void SQLServer::Done(const String& name, const String& alt_name)
	{
		const String _done_message_default_prefix = "Database.Task.Done.";
		if (alt_name != "")
			//RaiseEvent(alt_name);
			SendEvent(alt_name);
		if (alt_name.substr(0, _done_message_default_prefix.size()) != _done_message_default_prefix)
			//RaiseEvent("Database.Task.Done." + name);
			SendEvent("Database.Task.Done." + name);
	}

	void SQLServer::ReportError(const String& name, Error::ERROR_CODE code, const String& message, const String& alt_name)
	{
		const String _error_message_default_prefix = "Database.Error.";
		WriteValue("Database.Error.code", code);
		WriteValue("Database.Error.msg", message);
		String _error_code_name = Error::descriptor()->FindFieldByName("code")->enum_type()->FindValueByNumber((int)code)->name();
		LOG_E(TAG, "SQLServer::ReportError() code: %s, message: %s", _error_code_name.c_str(), message.c_str());
		if (alt_name != "")
			//RaiseEvent(alt_name);
			SendEvent(alt_name);
		if (alt_name.substr(0, _error_message_default_prefix.size()) != _error_message_default_prefix)
			//RaiseEvent("Database.Error." + name);
			SendEvent("Database.Error." + name);
	}

	String SQLServer::GetAlias(const String& var)
	{
		size_t _last_alias = var.rfind(" AS ");
		if (_last_alias != String::npos)
			return var.substr(_last_alias + sizeof(" AS ") - 1);
		else
			return "";
	}

	String SQLServer::RemoveAlias(const String& var)
	{
		size_t _last_alias = var.rfind(" AS ");
		if (_last_alias != String::npos)
			return var.substr(0, _last_alias);
		else
			return var;
	}

	String SQLServer::RemoveAllAlias(const String& var)
	{
		String _var = var;
		size_t _last_alias_start = var.rfind(" AS ");
		while (_last_alias_start != String::npos)
		{
			size_t _last_alias_end = _var.find_first_of(SQL_SYMBOLS, _last_alias_start + sizeof(" AS "));
			if (_last_alias_end == String::npos)
				_last_alias_end = _var.size();
			_var.replace(_last_alias_start, _last_alias_end - _last_alias_start, "");
			_last_alias_start = _var.rfind(" AS ");
		}
		return _var;
	}

	void SQLServer::ResetAll(bool create_stored_procedure)
	{
		Array<String> _table_list = ReadValue<Array<String>>("Database.Internal.TableList");
		for (auto _table_name : _table_list)
		{
			Remove(_table_name + ".*");
		}
		if (create_stored_procedure)
		{
			for (auto _elem : PROC_KEYWORDS)
			{
				//WriteValue(_elem, Array<String>());
				Remove(_elem);
			}
		}
		else
		{
			for (auto _elem : CLAUSE_KEYWORDS)
			{
				//WriteValue(_elem, Array<String>());
				Remove(_elem);
			}
		}
		modelname_function_map_.clear();
		column_list_.clear();
		quoted_column_list_.clear();
		//param_type_.clear();
	}

	void SQLServer::ConvertToDynamicFormat(Array<String>& statement, Array<String>& param_list)
	{
		Array<String> _cond_list = ReadValue<Array<String>>("Database.Internal.ConditionList");
		bool _need_convert_dynamic_format = false;
		for (auto _cond = _cond_list.begin(); _cond != _cond_list.end(); ++_cond)
		{
			if (_cond->front() != DYNAMIC_CONDITION_PREFIX)
				continue;
			auto _itr = std::find(param_list.begin(), param_list.end(), *_cond);
			if (_itr != param_list.end())
			{
				_need_convert_dynamic_format = true;
				break;
			}
		}

		if (_need_convert_dynamic_format == false)
		{
			Array<String> _filter_list = ReadValue<Array<String>>("Database.Internal.FilterList");
			for (auto _filter = _filter_list.begin(); _filter != _filter_list.end(); ++_filter)
			{
				auto _itr = std::find(param_list.begin(), param_list.end(), *_filter);
				if (_itr != param_list.end())
				{
					_need_convert_dynamic_format = true;
					break;
				}
			}
		}
		else
		{
			for (unsigned int i = 0; i < statement.size(); i++)
			{
				for (auto param_item : param_list)
				{
					auto _pos = statement[i].find(param_item);
					while (_pos != String::npos)
					{
						if (statement[i][0] == DYNAMIC_CONDITION_PREFIX)
						{	// dynamic conditions
							statement[i] = "(\", _sub_stmt,\")";
						}
						else
						{
							statement[i] = statement[i].substr(0, _pos) + "\", " + param_item + ", \"" + statement[i].substr(_pos + param_item.size());
						}
						_pos = statement[i].find(param_item, _pos + param_item.size());
					}
				}
			}
			statement.front() = "BEGIN\r\nDECLARE _stmt TEXT;\r\nDECLARE _sub_stmt TEXT;\r\n"
				"SET _sub_stmt = " + ReadValue<String>("Database.Internal.DynamicCondition") + ";\r\n"
				"SET _stmt = CONCAT(\"" + statement.front();
			statement.back() = statement.back() + "\");\r\n"
				"SET @_stmt=_stmt;\r\n"
				"PREPARE query_stmt FROM @_stmt;\r\n"
				"EXECUTE query_stmt;\r\nEND";
		}
	}
	void SQLServer::ModifyTableName(Array<String>& table_list)
	{
		for (auto& _table_name : table_list)
		{
			if (_table_name.find_first_of(SQL_SYMBOLS) != String::npos)
				continue;
			if (_table_name.front() == '[' && _table_name.back() == ']')
				continue;
			size_t _start_pos = _table_name.find('.');
			if (_start_pos == String::npos)
				continue;
			size_t _next_pos = _table_name.find('.', _start_pos + 1);
			if (_next_pos == String::npos)
			{
				_table_name = "[" + _table_name.substr(0, _start_pos) + "].[dbo].[" + _table_name.substr(_start_pos + 1) + "]";
				continue;
			}
			else
			{
				_table_name = "[" + _table_name.substr(0, _start_pos) + "].[dbo].[" + _table_name.substr(_start_pos + 1, _next_pos - _start_pos - 1) + "].[" + _table_name.substr(_next_pos + 1) + "]";
			}
		}
	}
	void SQLServer::ModifyColumnName(Array<String>& column_list)
	{
		for (auto& _column_name : column_list)
		{
			if (std::count(_column_name.begin(), _column_name.end(), '.') < 2)
			{
				if (std::count(_column_name.begin(), _column_name.end(), '_') > 0)
					_column_name = "@" + _column_name;
				continue;
			}
			size_t _start_pos = 0, _end_pos = String::npos;
			_end_pos = _column_name.find('.', _start_pos);
			_start_pos = _column_name.find_last_of(SQL_SYMBOLS, _end_pos - 1);
			if (_start_pos == String::npos)
				_start_pos = 0;
			else
				_start_pos++;
			if (_end_pos != String::npos && _start_pos != _end_pos && (_column_name[_start_pos] != '[' || _column_name[_end_pos - 1] != ']'))
			{
				_column_name = _column_name.substr(0, _start_pos) + "[" + _column_name.substr(_start_pos, _end_pos - _start_pos) + "].[dbo]" + _column_name.substr(_end_pos);
				_end_pos = _column_name.find_first_of(".", _end_pos + sizeof("].[dbo]."));
				_start_pos = _column_name.find_last_of(SQL_SYMBOLS + ".", _end_pos - 1) + 1;
				if (_column_name[_start_pos] == '.')
				{
					_start_pos++;
				}
			}
			while (_end_pos != String::npos)
			{
				if (_start_pos != _end_pos && (_column_name[_start_pos] != '[' || _column_name[_end_pos - 1] != ']'))
					_column_name = _column_name.substr(0, _start_pos) + "[" + _column_name.substr(_start_pos, _end_pos - _start_pos) + "]" + _column_name.substr(_end_pos);
				while (_end_pos != String::npos)
				{
					_end_pos = _column_name.find_first_of(".", _end_pos + sizeof("].") + 1);
					_start_pos = _column_name.find_last_of(SQL_SYMBOLS + ".", _end_pos - 1);
					if (_column_name[_start_pos] == '.')
					{
						_start_pos++;
						break;
					}
					else
					{	// must be the SQLServer.Internal.Name
						_start_pos++;
						if (_column_name.substr(_start_pos, _end_pos - _start_pos) == ReadValue<String>("SQLServer.Internal.Name"))
							break;
					}
				}
			}
		}
	}

	void SQLServer::doFunction()
	{
		int _function_type = -1;
		String _function_name = ReadValue<String>("Database.Function.name");
		if (is_number(_function_name))
		{
			_function_type = ReadValue<int>("Database.Function.name");
			_function_name = Function::FUNCTION_TYPE_descriptor()->FindValueByNumber(_function_type)->name();
		}
		else
		{
			_function_type = Function::FUNCTION_TYPE_descriptor()->FindValueByName(_function_name)->number();
		}
		String _function_alias = ReadValue<String>("Database.Function.alias");
		Array<String> _function_parameters = ReadValue<Array<String>>("Database.Function.parameters");
		//for (auto& _parameter : _function_parameters)
		//	_parameter.erase(std::remove(_parameter.begin(), _parameter.end(), ' '), _parameter.end());

		if (!_function_alias.empty())
		{
			String lowercase_alias(_function_alias.size(), '\0');
			std::transform(_function_alias.begin(), _function_alias.end(), lowercase_alias.begin(), ::tolower);
			if (_RESERVED_KEYWORD.find(lowercase_alias) != _RESERVED_KEYWORD.end())
			{
				ReportError(_function_name, Error::INVALID_DATA, "Invalid alias name \"" + _function_alias + "\" in function call: '" + _function_name + "'");
				return;
			}
		}

		String _params = "";
		String _target_model = ReadValue<String>("Database.Function.target_model_name");
		String _alias_target = "";
		if (!_function_alias.empty())
			_alias_target = ReadValue<String>("SQLServer.Internal.Name") + "." + ReadValue<String>("SQLServer.Internal.Title") + "." + _function_alias;
		//if (_target_model == "")
		//	_target_model = ReadValue<String>("SQLServer.Internal.Name") + "." + ReadValue<String>("SQLServer.Internal.Title") + "." + _function_alias;

		switch (_function_type)
		{
		case Function::DATE_FORMAT:
		{
			bool _remove_delimeters = ReadValue<bool>("Database.Function.remove_delimeters");
			for (int i = 0; i < _function_parameters.size(); i++)
			{
				String _parameter = GetParameter(_function_parameters[i]);
				if (i == 1)		// format of date-time
				{
					Array<String> _formats;
					_parameter.erase(std::remove(_parameter.begin(), _parameter.end(), ' '), _parameter.end());
					//std::replace(_parameter.begin(), _parameter.end(), '\"', '\'');
					_parameter = std::regex_replace(_parameter, std::regex((String)"(" + u8"”" + "|\")"), "'");
					Split(_parameter, _formats, "'-.:TZ");
					if (_remove_delimeters == true)
					{
						const char DELIMETERS[] = "-.:TZ";
						for (unsigned int i = 0; i < sizeof(DELIMETERS) - 1; i++)
						{
							_parameter.erase(std::remove(_parameter.begin(), _parameter.end(), DELIMETERS[i]), _parameter.end());
						}
					}
					for (unsigned int i = 0; i < _formats.size(); i++)
					{
						auto itr = format_map::datetime.find(_formats[i]);
						if (itr != format_map::datetime.end())
							_parameter = std::regex_replace(_parameter, std::regex(itr->first), itr->second[format_map::MSSQL]);
					}
				}
				_params += RemoveAlias(_parameter);
				if (i < _function_parameters.size() - 1)
					_params += ";";
			}
			if (_params != "")
			{
				String _result = _function_name + "(" + _params + ")";
				if (!_function_alias.empty())
					_result += " AS " + _function_alias;
				//String _result = _function_name + "(" + _params + ")";
				LOG_D(TAG, "SQLServer::doFunction() writing result \"%s\" to model \"%s\"", _result.c_str(), _target_model != "" ? _target_model.c_str() : _alias_target.c_str());
				WriteValue(_alias_target, _result);
				if (!_alias_target.empty())
				{
					WriteValue(_alias_target, _result);
					if (_target_model != "")
						Clone(_target_model, _alias_target);
					modelname_function_map_.insert(std::make_pair(_result, _alias_target));
				}
				else if (!_target_model.empty())
				{
					WriteValue(_target_model, _result);
					modelname_function_map_.insert(std::make_pair(_result, _target_model));
				}
			}
			else
			{
				LOG_W(TAG, "SQLServer::doFunction() Invalid parameters in function \"%s\"", _function_name.c_str());
			}
			break;
		}
		case Function::RIGHT_NOW:
		{
			_function_name = "NOW";
			for (int i = 0; i < _function_parameters.size(); i++)
			{
				_params += GetParameter(_function_parameters[i]);
				if (i < _function_parameters.size() - 1)
					_params += ";";
			}
			String _result = _function_name + "(" + _params + ")";
			if (!_function_alias.empty())
				_result += " AS " + _function_alias;
			LOG_D(TAG, "SQLServer::doFunction() writing result \"%s\" to model \"%s\"", _result.c_str(), _target_model != "" ? _target_model.c_str() : _alias_target.c_str());
			if (!_alias_target.empty())
			{
				WriteValue(_alias_target, _result);
				if (_target_model != "")
				{
					Clone(_target_model, _alias_target);
				}
				modelname_function_map_.insert(std::make_pair(_result, _alias_target));
			}
			else if (!_target_model.empty())
			{
				WriteValue(_target_model, _result);
				modelname_function_map_.insert(std::make_pair(_result, _target_model));
			}
			break;
		}
		case Function::UUID:
		{
			for (int i = 0; i < _function_parameters.size(); i++)
			{
				_params += GetParameter(_function_parameters[i]);
				if (i < _function_parameters.size() - 1)
					_params += ";";
			}
			String _result = _function_name + "(" + _params + ")";
			if (!_function_alias.empty())
				_result += " AS " + _function_alias;
			LOG_D(TAG, "SQLServer::doFunction() writing result \"%s\" to model \"%s\"", _result.c_str(), _target_model != "" ? _target_model.c_str() : _alias_target.c_str());
			if (!_alias_target.empty())
			{
				WriteValue(_alias_target, _result);
				if (_target_model != "")
				{
					Clone(_target_model, _alias_target);
				}
				modelname_function_map_.insert(std::make_pair(_result, _alias_target));
			}
			else if (!_target_model.empty())
			{
				WriteValue(_target_model, _result);
				modelname_function_map_.insert(std::make_pair(_result, _target_model));
			}
			break;
		}
		case Function::FORMAT:
		case Function::CONCAT:
		case Function::SUM:
		case Function::COUNT:
		case Function::MAX:
		case Function::MIN:
		{
			for (int i = 0; i < _function_parameters.size(); i++)
			{
				_params += RemoveAlias(GetParameter(_function_parameters[i]));
				if (i < _function_parameters.size() - 1)
					_params += ";";
			}
			if (_params != "")
			{
				String _result = _function_name + "(" + _params + ") AS " + _function_alias;
				LOG_D(TAG, "SQLServer::doFunction() writing result \"%s\" to model \"%s\"", _result.c_str(), _target_model != "" ? _target_model.c_str() : _alias_target.c_str());
				WriteValue(_alias_target, _result);
				if (_target_model != "")
					Clone(_target_model, _alias_target);
				modelname_function_map_.insert(std::make_pair(_result, _alias_target));
			}
			else
			{
				LOG_W(TAG, "SQLServer::doFunction() Invalid parameters in function \"%s\"", _function_name.c_str());
			}
			break;
		}
		case Function::BIN_TO_UUID:
		{
			for (int i = 0; i < _function_parameters.size(); i++)
			{
				_params += RemoveAlias(GetParameter(_function_parameters[i]));
				if (i < _function_parameters.size() - 1)
					_params += ";";
			}
			if (_params != "")
			{
				String _result = _function_name + "(" + _params + "; 1) AS " + _function_alias;
				LOG_D(TAG, "SQLServer::doFunction() writing result \"%s\" to model \"%s\"", _result.c_str(), _target_model != "" ? _target_model.c_str() : _alias_target.c_str());
				WriteValue(_alias_target, _result);
				if (_target_model != "")
					Clone(_target_model, _alias_target);
				modelname_function_map_.insert(std::make_pair(_result, _alias_target));
			}
			else
			{
				LOG_W(TAG, "SQLServer::doFunction() Invalid parameters in function \"%s\"", _function_name.c_str());
			}
			break;
		}
		case Function::UUID_TO_BIN:
		{
			for (int i = 0; i < _function_parameters.size(); i++)
			{
				bool _need_quote = false;
				String _param = RemoveAlias(GetParameter(_function_parameters[i]));
				bool _param_from_model = IsParameterFromModel(_function_parameters[i]);
				String _param_in_model = "";
				if (_param_from_model)
					_param_in_model = ReadValue<String>(_function_parameters[i].substr(2));
				if (_param != "UUID()" && (_param_from_model == false || _param_in_model != "") && is_uuid(_param) == true)
				{
					_need_quote = true;
				}
				if (_need_quote && (_param.front() != '\'' || _param.back() != '\''))
					_params += "'" + _param + "'";
				else
					_params += _param;
				if (i < _function_parameters.size() - 1)
					_params += ";";
			}
			if (_params != "")
			{
				String _result = _function_name + "(" + _params + "; 1) AS " + _function_alias;
				LOG_D(TAG, "SQLServer::doFunction() writing result \"%s\" to model \"%s\"", _result.c_str(), _target_model != "" ? _target_model.c_str() : _alias_target.c_str());
				WriteValue(_alias_target, _result);
				if (_target_model != "")
					Clone(_target_model, _alias_target);
				modelname_function_map_.insert(std::make_pair(_result, _alias_target));
			}
			else
			{
				LOG_W(TAG, "SQLServer::doFunction() Invalid parameters in function \"%s\"", _function_name.c_str());
			}
			break;
		}
		case Function::DATE_ADD:
		{
			if (_function_parameters.size() >= 3)
			{
				bool _need_quote = false;
				String _param_0 = RemoveAlias(GetParameter(_function_parameters[0]));
				bool _param_from_model = IsParameterFromModel(_param_0);
				String _param_in_model = "";
				if (_param_from_model)
					_param_in_model = ReadValue<String>(_param_0.substr(2));
				//if (_param_0 != "NOW()" && (_param_from_model == false || _param_in_model != ""))
				if (is_datetime(_param_0))
				{
					_need_quote = true;
				}
				if (_need_quote && (_param_0.front() != '\'' || _param_0.back() != '\''))
					_params += "'" + _param_0 + "'";
				else
					_params += _param_0;
				_params += ";INTERVAL " + GetParameter(_function_parameters[1]) + " " + GetParameter(_function_parameters[2]);
			}
			else
			{
				ReportError(_function_name, Error::INVALID_DATA, "Need 3 parameters in this function call '" + _function_name + "'");
				return;
			}
			String _result = _function_name + "(" + _params + ") AS " + _function_alias;
			LOG_D(TAG, "SQLServer::doFunction() writing result \"%s\" to model \"%s\"", _result.c_str(), _target_model != "" ? _target_model.c_str() : _alias_target.c_str());
			WriteValue(_alias_target, _result);
			if (_target_model != "")
				Clone(_target_model, _alias_target);
			modelname_function_map_.insert(std::make_pair(_result, _alias_target));
			break;
		}
		case Function::TIME_DIFF:
		{
			_function_name = "TIMESTAMPDIFF";
			if (_function_parameters.size() >= 3)
			{
				_params += GetParameter(_function_parameters[2]);
				_params += ";" + RemoveAlias(GetParameter(_function_parameters[0]));
				_params += ";" + RemoveAlias(GetParameter(_function_parameters[1]));
			}
			else
			{
				ReportError(_function_name, Error::INVALID_DATA, "Need 3 parameters in this function call '" + _function_name + "'");
				return;
			}
			String _result = _function_name + "(" + _params + ") AS " + _function_alias;
			LOG_D(TAG, "SQLServer::doFunction() writing result \"%s\" to model \"%s\"", _result.c_str(), _target_model != "" ? _target_model.c_str() : _alias_target.c_str());
			WriteValue(_alias_target, _result);
			if (_target_model != "")
				Clone(_target_model, _alias_target);
			modelname_function_map_.insert(std::make_pair(_result, _alias_target));
			break;
		}
		case Function::CALC:
			if (_function_parameters.size() >= 1)
			{
				String& _param = _function_parameters[0];
				size_t _start_pos = 0;
				if ((_start_pos = _param.find("::", _start_pos)) != String::npos)
				{
					while (_start_pos != String::npos)
					{
						_params += _param.substr(0, _start_pos);
						size_t _end_pos = _param.find_first_of(SQL_SYMBOLS, _start_pos + 2);
						if (_end_pos != String::npos)
						{
							_params += GetParameter(_param.substr(_start_pos, _end_pos - _start_pos - 2));
						}
						else
						{
							_params += GetParameter(_param.substr(_start_pos));
						}
						_start_pos = _param.find("::", _start_pos + 2);
					}
				}
				else
				{
					_params = _param;
				}
				String _result = _params + " AS " + _function_alias;
				LOG_D(TAG, "SQLServer::doFunction() writing result \"%s\" to model \"%s\"", _result.c_str(), _target_model != "" ? _target_model.c_str() : _alias_target.c_str());
				WriteValue(_alias_target, _result);
				if (_target_model != "")
					Clone(_target_model, _alias_target);
				modelname_function_map_.insert(std::make_pair(_result, _alias_target));
			}
			break;
		case Function::JSON_TABLE:
		{
			if (_function_parameters.size() != 2)
			{
				LOG_E(TAG, "SQLServer::doFunction() Invalid parameter count for JSON_TABLE, it must be 2. Parameters must be splitted by ','.");
				ReportError(_function_name, Error::INVALID_DATA, "Invalid parameters");
				return;
			}
			_params += GetParameter(_function_parameters[1]);
			std::replace(_function_parameters[0].begin(), _function_parameters[0].end(), ';', ',');
			_function_parameters[0].erase(std::remove(_function_parameters[0].begin(), _function_parameters[0].end(), '\"'), _function_parameters[0].end());
			WriteValue<String>("Database.Internal.Temp", _function_parameters[0]);
			Array<String> _column_name_list = ReadValue<Array<String>>("Database.Internal.Temp");
			if (_column_name_list.size() == 0)
				return;
			_params += ", '$[*]' COLUMNS(";
			for (int i = 0; i < _column_name_list.size(); i++)
			{
				_params += _column_name_list[i] + " VARCHAR(1024) PATH '$." + _column_name_list[i] + (i == _column_name_list.size() - 1 ? "')" : "',");
			}
			String _result = _function_name + "(" + _params + ") AS " + _function_alias;
			LOG_D(TAG, "SQLServer::doFunction() writing result \"%s\" to model \"%s\"", _result.c_str(), _target_model != "" ? _target_model.c_str() : _alias_target.c_str());
			WriteValue(_alias_target, _result);
			if (_target_model != "")
				Clone(_target_model, _alias_target);
			modelname_function_map_.insert(std::make_pair(_result, _alias_target));
			break;
		}
		default:
			ReportError(_function_name, Error::INVALID_DATA, "Unsupported function '" + _function_name + "'");
			break;
		}
	}

	void SQLServer::doSetup()
	{
		database_name_ = ReadValue<String>("Database.Setup.database");
		LOG_D(TAG, "##### SQLServer::doSetup ####");
		String _connect_string = string_format(
			"Driver={%s};"
			"Server=%s; Port=%s;"
			"Database=%s;"
			"Uid=%s;Pwd=%s;TrustServerCertificate=yes;",
			ReadValue<String>("Database.Setup.driver").c_str(),
			ReadValue<String>("Database.Setup.server").c_str(),
			ReadValue<String>("Database.Setup.port").c_str(),
			ReadValue<String>("Database.Setup.database").c_str(),
			ReadValue<String>("Database.Setup.uid").c_str(),
			ReadValue<String>("Database.Setup.pwd").c_str());

		// Set prefix to "SQLServer" to prevent impact of prefix "Database" used in MySQL
		WriteValue("SQLServer.Internal.Name", "");
		WriteValue("SQLServer.Internal.Title", "");

		ResetAll();

		bool _the_first_connection = true;
		std::function<ODBCAPI* ()> factory = [&]()
			{
				ODBCAPI* _db = new ODBCAPI();
				try
				{
					_db->connect(_connect_string);

					if (_the_first_connection == true)
					{
						SendEvent("Database.Setup.Successfully");
						_the_first_connection = false;
					}
				}
				catch (const std::exception& e)
				{
					LOG_E(TAG, "SQLServer::doSetup() Execption: %s", e.what());
					ReportError("Setup", Error::DATABASE_NOT_FOUND, "database name:" + ReadValue<String>("Database.Setup.database"));
				}
				return _db;
			};

		Clone("SQLServer.Internal.Name", "Database.Setup.database");
		String _title = ReadValue<String>("Bio.Cell.Current.FullNamespace");
		Array<String> _ns_list;
		Split(_title, _ns_list, ".");
		if (_ns_list.size() >= 2)
			WriteValue("SQLServer.Internal.Title", _ns_list[_ns_list.size() - 2]);
		else
			WriteValue("SQLServer.Internal.Title", _ns_list.back());

		if (db_connection_pool_ == nullptr)
		{
			unsigned int _pool_size = ReadValue<unsigned int>("Database.Internal.ConnectionPoolSize");
			if (_pool_size == 0)
			{
				_pool_size = std::thread::hardware_concurrency();
				WriteValue("Database.Internal.ConnectionPoolSize", _pool_size);
			}
			db_connection_pool_ = std::make_shared<ObjectPool<ODBCAPI>>(_pool_size, factory);

			unsigned int _interval = ReadValue<unsigned int>("Database.Internal.KeepAliveInterval");
			if (_interval == 0)
			{
				_interval = 3600000;
				WriteValue("Database.Internal.KeepAliveInterval", _interval);
			}
			KeepAlive(_interval, _pool_size);
		}
		else
		{
			SendEvent("Database.Setup.Successfully");
		}
	}

	void SQLServer::doCreateClause()
	{
		const bool CREATE_CLAUSE = false;
		WriteValue("Database.Internal.CommitContent", "");
		ConstAPI::PROC_TYPE _proc_type = (ConstAPI::PROC_TYPE)ReadValue<int>("Database.Internal.ProcType");
		String _clause_alias = ReadValue<String>("Database.CreateClause.alias");
		if (_clause_alias == "")
		{
			ReportError("SubClause", Error::INVALID_DATA, "No alias name found in create_clause()", ReadValue<String>("Database.CreateClause.err_message"));
			return;
		}
		String _target_model = ReadValue<String>("Database.CreateClause.target_model_name");
		String _alias_target = ReadValue<String>("SQLServer.Internal.Name") + "." + ReadValue<String>("SQLServer.Internal.Title") + "." + _clause_alias;
		Array<String> _statement;
		Array<String> _param_list = ReadValue<Array<String>>("Database.Internal.ParameterList");
		switch (_proc_type)
		{
		case ConstAPI::QUERY:
			if (CreateSelectStatement(_statement, _param_list, CREATE_CLAUSE) == false)
			{
				ReportError(_alias_target, Error::CREATE_PROC_ERROR, "Fail to create QUERY clause: '" + _alias_target + "'", ReadValue<String>("Database.CreateClause.err_message"));
				return;
			}
			for (unsigned int i = 0; i < _param_list.size(); i++)
				std::replace(_param_list[i].begin(), _param_list[i].end(), '.', '_');
			ConvertToDynamicFormat(_statement, _param_list);
			break;
		case ConstAPI::INSERT:
			if (CreateInsertStatement(_statement, _param_list) == false)
			{
				ReportError(_alias_target, Error::CREATE_PROC_ERROR, "Fail to create INSERT clause: '" + _target_model + "'", ReadValue<String>("Database.CreateClause.err_message"));
				return;
			}
			for (unsigned int i = 0; i < _param_list.size(); i++)
				std::replace(_param_list[i].begin(), _param_list[i].end(), '.', '_');
			ConvertToDynamicFormat(_statement, _param_list);
			break;
		case ConstAPI::UPDATE:
			if (CreateUpdateStatement(_statement, _param_list) == false)
			{
				ReportError(_alias_target, Error::CREATE_PROC_ERROR, "Fail to create UPDATE clause: '" + _target_model + "'", ReadValue<String>("Database.CreateClause.err_message"));
				return;
			}
			break;
		case ConstAPI::DELETE:
		{
			if (CreateDeleteStatement(_statement, _param_list) == false)
			{
				ReportError(_alias_target, Error::CREATE_PROC_ERROR, "Fail to create DELETE clause: '" + _target_model + "'", ReadValue<String>("Database.CreateClause.err_message"));
				return;
			}
			for (unsigned int i = 0; i < _param_list.size(); i++)
				std::replace(_param_list[i].begin(), _param_list[i].end(), '.', '_');
			ConvertToDynamicFormat(_statement, _param_list);
			break;
		}
		default:
			assert(false);
			ReportError(_target_model, Error::CREATE_PROC_ERROR, "Unknown procedure type: '" + std::to_string(_proc_type) + "'", ReadValue<String>("Database.CreateClause.err_message"));
			return;
		}

		String _output;
		for (unsigned int i = 0; i < _statement.size(); i++)
			_output += _statement[i];
		if (ReadValue<bool>("Database.CreateClause.show_result") == true)
		{
			LOG_I(TAG, "SQLServer::doCreateClause() statement: %s", _output.c_str());
		}
		WriteValue(_target_model, "(" + _output + ") AS " + _clause_alias);

		Array<String> _query_items = ReadValue<Array<String>>("Database.Internal.Columns");
		WriteValue("Database.Internal.LastSelectionCount", (unsigned long long)_query_items.size());

		String _name = ReadValue<String>("Database.CreateClause.owner");
		if (_name == "")
			_name = ReadValue<String>("SQLServer.Internal.Title");
		String _proc_full_name = ReadValue<String>("SQLServer.Internal.Name") + "." + _name;
		{
			MutexLocker proccall_info_lock_(proccall_info_mutex_);
			auto _itr = proccall_info_map_.find(_proc_full_name);
			if (_itr == proccall_info_map_.end())
			{
				proccall_info_map_.insert(std::make_pair(_proc_full_name, ProcedureInfo()));
				_itr = proccall_info_map_.find(_proc_full_name);
			}
			ProcedureInfo& _procedure_info = _itr->second;
			_procedure_info.name = _proc_full_name;
			_procedure_info.type = _proc_type;
			//Array<String>& _list = ReadValue<Array<String>>("Database.Internal.Columns");
			//_procedure_info.proceed_items.insert(_procedure_info.proceed_items.end(), _list.begin(), _list.end());
			//_list = ReadValue<Array<String>>("Database.Internal.TableList");
			//_procedure_info.table_list.insert(_procedure_info.table_list.end(), _list.begin(), _list.end());
			for (auto _column : column_list_)
			{
				_procedure_info.column_list.insert({ _column.first , _column.second });
			}
			//_procedure_info.column_list.insert(column_list_.begin(), column_list_.end());
			Array<String> _list = ReadValue<Array<String>>("Database.Internal.ParameterList");
			for (auto param : _list)
			{
				if (std::find(_procedure_info.param_list.begin(), _procedure_info.param_list.end(), param) == _procedure_info.param_list.end())
					_procedure_info.param_list.push_back(param);
			}
			_list = ReadValue<Array<String>>("Database.Internal.FilterList");
			_procedure_info.filter_list.insert(_procedure_info.filter_list.end(), _list.begin(), _list.end());
		}

		ResetAll();

		Done(_target_model, ReadValue<String>("Database.CreateClause.done_message"));
	}

	void SQLServer::KeepAlive(uint64_t interval, unsigned int pool_size)
	{
		timer_queue_.add(interval, [this, interval, pool_size](bool aborted) mutable {
			for (unsigned int i = 0; i < pool_size; i++)
			{
				Obj<ODBCAPI> _db = db_connection_pool_->borrow();
				_db->execute("SELECT @@version");
			}
			this->KeepAlive(interval, pool_size);
			});
	}

	bool SQLServer::is_datetime(const String& data)
	{
		//####-##-##T##:##:##.######
		bool _ret = true;
		String _parameter = data;
		Array<String> _datetime_list;
		_parameter.erase(std::remove(_parameter.begin(), _parameter.end(), ' '), _parameter.end());
		std::replace(_parameter.begin(), _parameter.end(), '\"', '\'');
		Split(_parameter, _datetime_list, "'+-.:T");
		if (_datetime_list.size() < 3)
			_ret = false;
		else
		{
			for (unsigned int i = 0; i < _datetime_list.size(); i++)
			{
				if (!is_digital(_datetime_list[i]))
				{
					_ret = false;
					break;
				}
			}
		}
		return _ret;
	}

	void SQLServer::doExecute()
	{
		LOG_D(TAG, "Database::doExecute()");
		// add schema name: dbo
		Array<String> _query_items = ReadValue<Array<String>>("Database.Internal.Columns");
		//PatchSchemaToName(_query_items);
		//WriteValue("Database.Internal.Columns", _query_items);
		String _name = ReadValue<String>("Database.Execute.name");
		String _database_name = ReadValue<String>("SQLServer.Internal.Name");
		ConstAPI::PROC_TYPE _proc_type = (ConstAPI::PROC_TYPE)ReadValue<int>("Database.Internal.ProcType");
		String _proc_full_name = "";
		if (_query_items.size() == 0 && _proc_type == ConstAPI::QUERY)
		{
			return;
		}
		if (_name == "")
		{
			_name = ReadValue<String>("SQLServer.Internal.Title");
			_proc_full_name = _database_name + "." + _name;
		}
		else if (_name.substr(0, _database_name.size()) != _database_name)
		{
			_proc_full_name = _database_name + "." + _name;
		}
		else
		{
			_proc_full_name = _name;
			_name = _name.substr(_database_name.size() + 1);
		}

		String _output = "";
		String _full_script = ReadValue<String>("Database.Execute.full_script");
		if (_full_script != "")
		{
			//Array<String> _statement = ReadValue<Array<String>>("Database.Internal.Statement");
			switch (_proc_type)
			{
			case ConstAPI::QUERY:
			{
				_output += _full_script;
				WriteValue("Database.Internal.CommitContent", _output);

				if (ReadValue<bool>("Database.Execute.show_result") == true)
				{
					LOG_I(TAG, "[%p] doExecute() statement: %s", this, ReadValue<String>("Database.Internal.CommitContent").c_str());
				}
				try
				{
					Array<String> _query_items = ReadValue<Array<String>>("Database.Internal.Columns");
					Array<int> _index_mapping;
					for (unsigned int i = 0; i < _query_items.size(); i++)
						_index_mapping.push_back(i);

					nanodbc::result _result;
					{
						if (db_connection_pool_ == nullptr)
						{
							ReportError(_proc_full_name, Error::DATABASE_NOT_SETUP_YET, "Please \"Setup\" database connection first", ReadValue<String>("Database.Execute.err_message"));
							return;
						}
						Obj<ODBCAPI> _db = db_connection_pool_->borrow();
						_result = _db->execute(_output, MAX_QUERY_RESULT_COUNT);
					}
					assert(_result.columns() == _query_items.size());

					WriteValue(_proc_full_name + ".RowCount", (int)_result.affected_rows());
					WriteValue(_proc_full_name + ".ColumnCount", (unsigned long long)_query_items.size());
					WriteValue(_proc_full_name + ".Columns", _query_items);

					String _callback_message_name = ReadValue<String>("Database.Execute.callback_message");
					Remove(_callback_message_name + ".*");		// to prevent data interference

					Array<String> _columns_model_list;
					//for (auto _name : _query_items)
					//	_columns_model_list.push_back((String)"::" + _name);
					//WriteValue(_callback_message_name + "." + TAG_COLUMNS_SOURCE_MODEL_LIST, _columns_model_list);
					for (int i = 0; i < _query_items.size(); i++)
						_columns_model_list.push_back((String)"::" + _callback_message_name + ".column_" + std::to_string(i));
					//WriteValue(_callback_message_name + "." + TAG_COLUMNS_SOURCE_MODEL_LIST, _columns_model_list);

					int _record_count = 0;
					nanodbc::string const null_value = NANODBC_TEXT("null");
					while (_result.next())
					{
						//LOG_D(TAG, "**********  Record %d  **********", _record_count);
						_record_count++;
						WriteValue(_callback_message_name + "." + TAG_COLUMNS_SOURCE_MODEL_LIST, _columns_model_list);
						for (unsigned int i = 0; i < _query_items.size(); i++)
						{
							String _column_name = _callback_message_name + ".column_" + std::to_string(i);
							try
							{
								WriteValue(_column_name, _result.get<nanodbc::string>(_index_mapping[i], null_value));
							}
							catch (...)
							{
								WriteValue(_column_name, null_value);
							}
							//LOG_D(TAG, "column(\"%s\") = %s", _column_name.c_str(), ReadValue<String>(_column_name).c_str());
						}
						//RaiseEvent(_callback_message_name);
						SendEvent(_callback_message_name);
						//Remove(_callback_message_name + ".*");		// to prevent data interference
					}

					ResetAll();

					Done(_proc_full_name, ReadValue<String>("Database.Execute.done_message"));
				}
				catch (const std::exception& e)
				{
					LOG_E(TAG, "[%p] doExecute() Execption: %s", this, e.what());
					ReportError(_proc_full_name, Error::EXEC_STATEMENT_ERROR, e.what(), ReadValue<String>("Database.Execute.err_message"));
				}
				break;
			}
			default:
				switch (_proc_type)
				{
				case ConstAPI::INSERT:
				case ConstAPI::UPDATE:
				case ConstAPI::DELETE:
				{
					_output += _full_script;
					WriteValue("Database.Internal.CommitContent", _output);

					if (ReadValue<bool>("Database.Execute.show_result") == true)
					{
						LOG_I(TAG, "[%p] doExecute() statement: %s", this, ReadValue<String>("Database.Internal.CommitContent").c_str());
					}
					try
					{
						nanodbc::result _result;
						{
							if (db_connection_pool_ == nullptr)
							{
								ReportError(_proc_full_name, Error::DATABASE_NOT_SETUP_YET, "Please \"Setup\" database connection first", ReadValue<String>("Database.Execute.err_message"));
								return;
							}
							Obj<ODBCAPI> _db = db_connection_pool_->borrow();
							_result = _db->execute(_output);
						}

						WriteValue(_proc_full_name + ".RowCount", (int)_result.affected_rows());

						ResetAll();

						Done(_proc_full_name, ReadValue<String>("Database.Execute.done_message"));
					}
					catch (const std::exception& e)
					{
						LOG_E(TAG, "[%p] doExecute() Execption: %s", this, e.what());
						ReportError(_proc_full_name, Error::EXEC_STATEMENT_ERROR, e.what(), ReadValue<String>("Database.Execute.err_message"));
					}
					break;
				}
				}
				break;
			}
		}
		else
		{
			Array<String> _table_list = ReadValue<Array<String>>("Database.Internal.TableList");
			//PatchSchemaToName(_table_list);
			//WriteValue("Database.Internal.TableList", _table_list);
			Array<String> _cond_list = ReadValue<Array<String>>("Database.Internal.ConditionList");
			//PatchSchemaToName(_cond_list);
			//WriteValue("Database.Internal.ConditionList", _cond_list);
			Array<String> _modifier_list = ReadValue<Array<String>>("Database.Internal.ModifierList");
			//PatchSchemaToName(_modifier_list);
			//WriteValue("Database.Internal.ModifierList", _modifier_list);

			WriteValue("Database.Internal.CommitContent", "");
			ConstAPI::PROC_TYPE _proc_type = (ConstAPI::PROC_TYPE)ReadValue<int>("Database.Internal.ProcType");

			String _name = ReadValue<String>("Database.Execute.name");
			String _database_name = ReadValue<String>("SQLServer.Internal.Name");
			String _proc_full_name = "";
			if (_name == "")
			{
				_name = ReadValue<String>("SQLServer.Internal.Title");
				_proc_full_name = _database_name + "." + _name;
			}
			else if (_name.substr(0, _database_name.size()) != _database_name)
			{
				_proc_full_name = _database_name + "." + _name;
			}
			else
			{
				_proc_full_name = _name;
				_name = _name.substr(_database_name.size() + 1);
			}

			String _output = "";
			switch (_proc_type)
			{
			case ConstAPI::QUERY:
			{
				Array<String> _statement;
				Array<String> _param_list = ReadValue<Array<String>>("Database.Internal.ParameterList");
				if (CreateSelectStatement(_statement, _param_list) == false)
					return;

				_statement.push_back(";\r\n");

				_output += "BEGIN\r\n";

				for (unsigned int i = 0; i < _statement.size(); i++)
					_output += _statement[i];
				_output += "END";
				WriteValue("Database.Internal.CommitContent", _output);

				if (ReadValue<bool>("Database.Execute.show_result") == true)
				{
					LOG_I(TAG, "Database::doExecute() statement: %s", ReadValue<String>("Database.Internal.CommitContent").c_str());
				}
				try
				{
					Array<String> _query_items = ReadValue<Array<String>>("Database.Internal.Columns");
					Array<int> _index_mapping;
					for (unsigned int i = 0; i < _query_items.size(); i++)
						_index_mapping.push_back(i);

					nanodbc::result _result;
					{
						if (db_connection_pool_ == nullptr)
						{
							ReportError(_proc_full_name, Error::DATABASE_NOT_SETUP_YET, "Please \"Setup\" database connection first", ReadValue<String>("Database.Execute.err_message"));
							return;
						}
						Obj<ODBCAPI> _db = db_connection_pool_->borrow();
						_result = _db->execute(_output, MAX_QUERY_RESULT_COUNT);
					}
					assert(_result.columns() == _query_items.size());

					WriteValue(_proc_full_name + ".RowCount", (int)_result.affected_rows());
					WriteValue(_proc_full_name + ".ColumnCount", (unsigned long long)_query_items.size());
					WriteValue(_proc_full_name + ".Columns", _query_items);

					String _callback_message_name = ReadValue<String>("Database.Execute.callback_message");
					Remove(_callback_message_name + ".*");		// to prevent data interference

					Array<String> _columns_model_list;
					//for (auto _name : _query_items)
					//	_columns_model_list.push_back((String)"::" + _name);
					//WriteValue(_callback_message_name + "." + TAG_COLUMNS_SOURCE_MODEL_LIST, _columns_model_list);
					for (int i = 0; i < _query_items.size(); i++)
						_columns_model_list.push_back((String)"::" + _callback_message_name + ".column_" + std::to_string(i));
					//WriteValue(_callback_message_name + "." + TAG_COLUMNS_SOURCE_MODEL_LIST, _columns_model_list);

					int _record_count = 0;
					nanodbc::string const null_value = NANODBC_TEXT("null");
					while (_result.next())
					{
						//LOG_D(TAG, "**********  Record %d  **********", _record_count);
						_record_count++;
						WriteValue(_callback_message_name + "." + TAG_COLUMNS_SOURCE_MODEL_LIST, _columns_model_list);
						for (unsigned int i = 0; i < _query_items.size(); i++)
						{
							String _column_name = _callback_message_name + ".column_" + std::to_string(i);
							try
							{
								//WriteValue(_column_name, AnsiStringToUTF8String(_result.get<nanodbc::string>(_index_mapping[i], null_value), "zh_TW"));
								WriteValue(_column_name, _result.get<nanodbc::string>(_index_mapping[i], null_value));
							}
							catch (...)
							{
								WriteValue(_column_name, null_value);
							}
							//LOG_D(TAG, "column(\"%s\") = %s", _column_name.c_str(), ReadValue<String>(_column_name).c_str());
						}
						SendEvent(_callback_message_name);
						//Remove(_callback_message_name + ".*");		// to prevent data interference
					}

					ResetAll();

					Done(_proc_full_name, ReadValue<String>("Database.Execute.done_message"));
				}
				catch (const std::exception& e)
				{
					LOG_E(TAG, "Database.doExecute() Execption: %s", e.what());
					ReportError(_proc_full_name, Error::EXEC_STATEMENT_ERROR, e.what(), ReadValue<String>("Database.Execute.err_message"));
				}
				break;
			}
			default:
			{
				Array<String> _statement;
				Array<String> _inner_statement;
				Array<String> _param_list = ReadValue<Array<String>>("Database.Internal.ParameterList");
				switch (_proc_type)
				{
				case ConstAPI::INSERT:
					break;
				case ConstAPI::UPDATE:
					if (CreateUpdateStatement(_inner_statement, _param_list) == false)
						return;
					for (unsigned int i = 0; i < _param_list.size(); i++)
					{
						auto _itr = column_list_.end();
						if ((_itr = column_list_.find(_param_list[i])) != column_list_.end())
						{
							std::replace(_param_list[i].begin(), _param_list[i].end(), '.', '_');
							//if (_itr->second.data_type_name_.find("binary") != String::npos)
							//	param_type_[_param_list[i]] = BINARY;
							//else if (_itr->second.data_type_name_.find("int") != String::npos)
							//	param_type_[_param_list[i]] = INT;
						}
						else
						{
							std::replace(_param_list[i].begin(), _param_list[i].end(), '.', '_');
						}
					}
					break;
				case ConstAPI::DELETE:
				{
					break;
				}
				default:
					assert(false);
					return;
				}
				//bool _inside_transaction = ReadValue<bool>("Database.Execute.rollback_if_error");
				//if (_inside_transaction)
				//{
				//	_statement.push_back(
				//		"DECLARE EXIT HANDLER FOR SQLEXCEPTION\r\n"
				//		"BEGIN\r\n"
				//		"	ROLLBACK;		-- rollback any changes made in the transaction\r\n"
				//		"	RESIGNAL;		-- raise again the sql exception to the caller\r\n"
				//		"END;\r\n"
				//		"START TRANSACTION;\r\n");
				//}

				for (auto _batch_item : ReadValue<Array<String>>("Database.Execute.batch_list"))
				{
					String _clause = RemoveAlias(ReadValue<String>(_batch_item));
					if (_clause.size() < 2)
					{
						ReportError(_proc_full_name, Error::INVALID_DATA, "No such clause: " + _batch_item, ReadValue<String>("Database.Execute.err_message"));
						return;
					}
					if (_clause.front() == '(' && _clause.back() == ')')
						_clause = _clause.substr(1, _clause.size() - 2);
					_statement.push_back(_clause);
					_statement.push_back(";\r\n");
				}

				_statement.insert(_statement.end(), std::make_move_iterator(_inner_statement.begin()), std::make_move_iterator(_inner_statement.end()));

				_statement.push_back(";\r\n");

				//if (_inside_transaction)
				//{
				//	_statement.push_back("COMMIT;\r\n");
				//}
				_output += "BEGIN\r\n";

				for (unsigned int i = 0; i < _statement.size(); i++)
					_output += _statement[i];
				_output += "END";
				WriteValue("Database.Internal.CommitContent", _output);

				if (ReadValue<bool>("Database.Execute.show_result") == true)
				{
					LOG_I(TAG, "Database.doExecute() statement: %s", ReadValue<String>("Database.Internal.CommitContent").c_str());
				}
				try
				{
					nanodbc::result _result;
					{
						if (db_connection_pool_ == nullptr)
						{
							ReportError(_proc_full_name, Error::DATABASE_NOT_SETUP_YET, "Please \"Setup\" database connection first", ReadValue<String>("Database.Execute.err_message"));
							return;
						}
						Obj<ODBCAPI> _db = db_connection_pool_->borrow();
						_result = _db->execute(_output);
					}

					WriteValue(_proc_full_name + ".RowCount", (int)_result.affected_rows());

					ResetAll();

					Done(_proc_full_name, ReadValue<String>("Database.Execute.done_message"));
				}
				catch (const std::exception& e)
				{
					LOG_E(TAG, "Database.doExecute() Execption: %s", e.what());
					ReportError(_proc_full_name, Error::EXEC_STATEMENT_ERROR, e.what(), ReadValue<String>("Database.Execute.err_message"));
					return;
				}
				break;
			}
			}
		}
	}

	void SQLServer::PatchSchemaToName(Array<String>& name_list)
	{
		for (auto& _name : name_list)
		{
			if (_name.find_first_of(SQL_SYMBOLS) != String::npos)
				continue;
			size_t _start_pos = _name.find('.');
			if (_start_pos == String::npos)
				continue;
			_name = _name.substr(0, _start_pos) + ".dbo." + _name.substr(_start_pos + 1);
		}
	}

	std::string SQLServer::AnsiStringToUTF8String(const std::string& str, const std::string& loc)
	{
		using namespace std;
		class mycodecvt : public codecvt_byname<wchar_t, char, mbstate_t> {
		public:
			mycodecvt(const string& loc) : codecvt_byname(loc) {}
		};
		wstring_convert<codecvt_utf8<wchar_t>> Conver_UTF8;
		wstring_convert<mycodecvt> Conver_Locale(new mycodecvt(loc));
		return Conver_UTF8.to_bytes(Conver_Locale.from_bytes(str));
	}

	std::string SQLServer::UTF8StringToAnsiString(const std::string& str, const std::string& loc)
	{
		using namespace std;
		class mycodecvt : public codecvt_byname<wchar_t, char, mbstate_t> {
		public:
			mycodecvt(const string& loc) : codecvt_byname(loc) {};
		};
		wstring_convert<codecvt_utf8<wchar_t>> Conver_UTF8;
		wstring_convert<mycodecvt> Conver_Locale(new mycodecvt(loc));
		return Conver_Locale.to_bytes(Conver_UTF8.from_bytes(str));
	}
	String SQLServer::GetColumnType(const Map<String, SchemaInfo>& column_list, const String& column_name)
	{
		String _data_type = "";
		const auto& schema_info = column_list.find(column_name);
		if (schema_info != column_list.end())
		{
			_data_type = schema_info->second.data_type_name_;
		}
		return _data_type;
	}
}