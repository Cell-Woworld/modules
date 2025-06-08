#include "SQLServer.h"

namespace Database
{
	bool SQLServer::CreateDeleteStatement(Array<String>& statement, Array<String>& param_list)
	{
		Array<String> _statement_prefix = ReadValue<Array<String>>("Database.Internal.Statement");
		Array<String> _delete_items = ReadValue<Array<String>>("Database.Internal.Columns");
		Array<String> _table_list = ReadValue<Array<String>>("Database.Internal.TableList");
		Array<String> _cond_list = ReadValue<Array<String>>("Database.Internal.ConditionList");
		if (_delete_items.size() == 0)
		{
			if (_table_list.size() > 1 || _table_list.size() == 0)
			{
				ReportError("Delete", Error::INVALID_DATA, "Unsupported deleting data from zero or more than one table, but there are " + std::to_string(_table_list.size()) + " tables");
				for (const auto& table : _table_list)
					LOG_E(TAG, "table name = %s", table.c_str());
				return false;
			}
			_delete_items = _table_list;
		}

		{	// add the last few ")"
			int _last_state_depth = ReadValue<int>("Database.Internal.LastDepth");
			String _current_state = ReadValue<String>("Bio.Cell.Current.State");
			int _current_state_depth = (int)std::count(_current_state.begin(), _current_state.end(), '/') + 1;
			if (_current_state_depth < _last_state_depth)
			{
				for (int i = 0; i < _last_state_depth - _current_state_depth; i++)
					_cond_list.push_back(")");
			}
		}

		ModifyTableName(_table_list);
		const String& _table_name = _delete_items[0];
		statement.insert(statement.end(), _statement_prefix.begin(), _statement_prefix.end());
		statement.push_back(_table_name);
		if (_table_list.size() > 0)
		{
			statement.push_back(" FROM ");
			for (unsigned int i = 0; i < _table_list.size(); i++)
			{
				if (i < _table_list.size() - 1)
					statement.push_back(_table_list[i] + ", ");
				else
					statement.push_back(_table_list[i]);
			}
		}
		if (_cond_list.size() > 0)
		{
			ModifyColumnName(_cond_list);
			statement.push_back(" WHERE ");
			for (unsigned int i = 0; i < _cond_list.size(); i++)
			{
				std::replace(_cond_list[i].begin(), _cond_list[i].end(), ';', ',');
				statement.push_back(_cond_list[i]);
			}
		}
		return true;
	}
}