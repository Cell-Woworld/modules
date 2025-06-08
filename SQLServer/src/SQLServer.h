#pragma once
#include "RNA.h"
#include "../proto/DatabaseAPI.pb.h"
#include "../proto/DatabaseShared.pb.h"
#include "../inc/pool.h"
#include "../inc/TimerQueue/TimerQueue.h"
#include "ODBCAPI.h"
#include <cctype>
#include <iostream>
#include <regex>

#define TAG "SQLServer"

namespace Database
{
	class format_map
	{
	public:
		enum
		{
			MYSQL,
			MSSQL,
			ORACLE,
		};
		static const Map<String, Array<String>> datetime;
		static const Map<String, Array<String>> keyword;
	};
	struct SchemaInfo
	{
		SchemaInfo(int index, const String& type_name) {
			index_ = index;
			data_type_name_ = type_name;
		};
		int index_;
		String data_type_name_;
	};
	struct ProcedureInfo
	{
		String name;
		ConstAPI::PROC_TYPE type;
		Array<String> proceed_items;
		Array<String> table_list;
		Map<String, SchemaInfo> column_list;
		Array<String> filter_list;
		Array<String> param_list;
	};
	struct ProcedureCallInfo
	{
		String event_name;
		String event_content;
		unsigned long long src_instance;
	};

	class SQLServer : public BioSys::RNA
	{
		static const int MAX_QUERY_RESULT_COUNT = 10000;
		static const String MAX_VCHAR_PARAM_SIZE;
		static const String MAX_BINARY_PARAM_SIZE;
		static const String MAX_MODEL_NAME_SIZE;
		static const Set<String> _RESERVED_KEYWORD;
		static const Set<String> NON_QUOTED_KEYWORD;
		static const String PROC_KEYWORDS[];
		static const String CLAUSE_KEYWORDS[];
		static const Map<String, String> SQL_KEYWORD;
		static const String SQL_SYMBOLS;
		static const char DYNAMIC_CONDITION_PREFIX = '#';
		enum MODIFIER_ORDER
		{
			GROUP_BY = 0,
			HAVING,
			ORDER_BY,
			PAGING,
			MAX_MODIFIER_ORDER,
		};
		enum DATA_TYPE
		{
			STRING = 0,
			INT,
			BINARY,
		};
	public:
		PUBLIC_API SQLServer(BioSys::IBiomolecule* owner);
		PUBLIC_API virtual ~SQLServer();

	protected:
		virtual void OnEvent(const DynaArray& name);

	private:
		void doCreateStoredProc();
		void doCallStoredProc();
		void doFunction();
		void doSetup();
		void doCreateClause();
		void doExecute();

		template<typename ... Args>
		std::string string_format(const std::string& format, Args ... args) {
			size_t size = snprintf(nullptr, 0, format.c_str(), args ...) + 1; // Extra space for '\0'
			std::unique_ptr<char[]> buf(new char[size]);
			snprintf(buf.get(), size, format.c_str(), args ...);
			return std::string(buf.get(), buf.get() + size - 1); // We don't want the '\0' inside
		};
		bool is_digital(const String& s) {
			return !s.empty() && std::find_if(s.begin(),
				s.end(), [](char c) { return !std::isdigit(c); }) == s.end();
		};
		bool is_number(const String& token) {
			return std::regex_match(token, std::regex(("((\\+|-)?[[:digit:]]+)(\\.(([[:digit:]]+)?))?")));
		}
		bool is_uuid(const String& token) {
			return std::regex_match(token, std::regex("[0-9a-fA-F]{8}\\-[0-9a-fA-F]{4}\\-[0-9a-fA-F]{4}\\-[0-9a-fA-F]{4}\\-[0-9a-fA-F]{12}"));
		}
		bool is_datetime(const String& data);

		bool CreateSelectStatement(Array<String>& statement, Array<String>& param_list, bool create_stored_procedure = true);
		bool CreateInsertStatement(Array<String>& statement, Array<String>& param_list);
		bool CreateUpdateStatement(Array<String>& statement, Array<String>& param_list);
		bool CreateDeleteStatement(Array<String>& statement, Array<String>& param_list);

		String GetParameter(const String& parameter);
		bool IsParameterFromModel(const String& parameter);
		void Split(const String& input, Array<String>& output, const String& delemiters, const Array<Pair<String, String>>& ignored_pairs = Array<Pair<String, String>>());
		void Done(const String& name, const String& alt_name = "");
		void ReportError(const String& name, Error::ERROR_CODE code, const String& message, const String& alt_name = "");
		template<typename T> void GetAllSchema(const T& table_list);
		String GetAlias(const String& var);
		String RemoveAlias(const String& var);
		String RemoveAllAlias(const String& var);
		void ResetAll(bool create_stored_procedure = true);
		void ConvertToDynamicFormat(Array<String>& statement, Array<String>& param_list);
		void ModifyTableName(Array<String>& table_list);
		void ModifyColumnName(Array<String>& column_list);
		void KeepAlive(uint64_t interval, unsigned int pool_size);
		void NextCall();
		String GetColumnType(const Map<String, SchemaInfo>& column_list, const String& column_name);
		// trim from start (in place)
		String ltrim(const String& s) {
			String _ret = s;
			_ret.erase(_ret.begin(), std::find_if(_ret.begin(), _ret.end(), [](unsigned char ch) {
				return !std::isspace(ch);
				}));
			return _ret;
		}

		// trim from end (in place)
		String rtrim(const String& s) {
			String _ret = s;
			_ret.erase(std::find_if(_ret.rbegin(), _ret.rend(), [](unsigned char ch) {
				return !std::isspace(ch);
				}).base(), _ret.end());
			return _ret;
		}
		void PatchSchemaToName(Array<String>& name_list);
		String AnsiStringToUTF8String(const String& str, const String& loc = std::locale{}.name());
		String UTF8StringToAnsiString(const String& str, const String& loc = std::locale{}.name());

	private:
		static Map<String, ProcedureInfo> proccall_info_map_;
		static Mutex proccall_info_mutex_;
		static Obj<ObjectPool<ODBCAPI>> db_connection_pool_;
		static TimerQueue timer_queue_;

		Map<String, String> modelname_function_map_;
		Map<String, SchemaInfo> quoted_column_list_;
		Map<String, SchemaInfo> column_list_;
		Map<String, DATA_TYPE> param_type_;
		Queue<ProcedureCallInfo> procedure_call_queue_;

		BioSys::IBiomolecule* owner_;
		String database_name_;
	};

	template<typename T>
	void SQLServer::GetAllSchema(const T& table_list)
	{
		enum {
			//ID,
			FIELD_NAME,
			TYPE,
			NULLTABLE
		};
		column_list_.clear();
		quoted_column_list_.clear();
		//param_type_.clear();
		size_t _count_base = 0;
		T _table_list = table_list;
		PatchSchemaToName(_table_list);
		for (auto _table_name : _table_list)
		{
			if (_table_name.find_first_of(SQL_SYMBOLS) != String::npos)
				continue;
			Array<String> _table_deatil_name;
			Split(_table_name, _table_deatil_name, ".");
			// get table schema
			Obj<ODBCAPI> _db = db_connection_pool_->borrow();
			nanodbc::result _results;
			if (_table_deatil_name.size() >= 3)
			{
				_results = _db->execute(NANODBC_TEXT((String)
					"SELECT COLUMN_NAME, DATA_TYPE, IS_NULLABLE "
					"FROM INFORMATION_SCHEMA.COLUMNS "
					"WHERE TABLE_CATALOG = '" + _table_deatil_name[0] + "' AND TABLE_SCHEMA='" + _table_deatil_name[1] + "' AND TABLE_NAME = '" + _table_deatil_name[2] + "'"));
			}
			else
			{
				LOG_E(TAG, "Invalid table name: %s", _table_name.c_str());
				continue;
			}

			int i = 0;
			for (int i=0; _results.next()==true; i++)
			{
				String _column_name = _table_deatil_name[0] + "."  + _table_deatil_name[2] + "." + _results.get<nanodbc::string>(FIELD_NAME, NANODBC_TEXT("null"));
				String _type_name = _results.get<nanodbc::string>(TYPE, NANODBC_TEXT("nvarchar"));
				column_list_.insert(std::make_pair(
					_column_name,
					SchemaInfo(i + (int)_count_base, _type_name)
				));
				String _quoted_column_name = "[" + _table_deatil_name[0] + "]." + "[" + _table_deatil_name[1] + "]." + "[" + _table_deatil_name[2] + "]." + _results.get<nanodbc::string>(FIELD_NAME, NANODBC_TEXT("null"));
				quoted_column_list_.insert(std::make_pair(
					_quoted_column_name,
					SchemaInfo(i + (int)_count_base, _type_name)
				));
			}
			_count_base += _results.affected_rows();
		}
	}

}