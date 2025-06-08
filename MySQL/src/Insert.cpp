#include "MySQL.h"

namespace Database
{
	bool MySQL::CreateInsertStatement(Array<String>& statement, Array<String>& param_list)
	{
		Array<String> _statement_prefix = ReadValue<Array<String>>("Database.Internal.Statement");
		Array<String> _insert_items = ReadValue<Array<String>>("Database.Internal.Columns");
		Array<String> _table_list = ReadValue<Array<String>>("Database.Internal.TableList");
		Array<String> _filter_list = ReadValue<Array<String>>("Database.Internal.FilterList");
		String _insert_query = ReadValue<String>("Database.Internal.QueryClause");
		std::unordered_set<String> _table_set(_table_list.begin(), _table_list.end());
		Array<String> _value_list;

		if (_table_list.size() == 0)
		{
			ReportError("Insert", Error::INVALID_DATA, "Table names needed. If columns are provided at run-time, please add table names with the \"TABLE\" condition");
			return false;
		}

		Array<String> _quoted_table_list = Array<String>(_table_set.begin(), _table_set.end());
		ModifyTableName(_quoted_table_list);
		// get schema from DB
		try
		{
			GetAllSchema(_quoted_table_list);
		}
		catch (const std::exception & e)
		{
			LOG_E(TAG, "MySQL::CreateInsertStatement() Execption: %s", e.what());
			ReportError("Insert", Error::INVALID_DATA, e.what());
			return false;
		}

		bool _is_values_type = (_insert_query == "");

		if (_insert_items.size() == 1 && _insert_items[0] == "*")
		{
			if (_is_values_type == false)
			{
				ReportError("Insert", Error::INVALID_DATA, "Assigning insert items dynamically with subquery is not supported");
				return false;
			}

			//Array<String> _quoted_insert_items;
			_insert_items.clear();
			for (auto _column : column_list_)
			{
				//String _column_name = _column.first;
				//_column_name.erase(std::remove(_column_name.begin(), _column_name.end(), '`'), _column_name.end());
				//_insert_items.push_back(_column_name);
				_insert_items.push_back(_column.first);
			}
			WriteValue("Database.Internal.Columns", _insert_items);
		}
		else
		{	// check duplication of insert_items
			Set<String> _insert_set;
			for (auto _insert_item : _insert_items)
			{
				if (_insert_set.find(_insert_item) != _insert_set.end())
				{
					ReportError("Insert", Error::INVALID_DATA, "Duplicate elements in the model list of Database.insert(). name: " + _insert_item);
					return false;
				}
				else
				{
					_insert_set.insert(_insert_item);
				}
			}
		}

		// to keep the order of insert_items and param_set are the same
		if (_is_values_type == true)
		{
			//param_list.insert(param_list.begin(), _insert_items.begin(), _insert_items.end());
			for (auto _insert_item : _insert_items)
			{
				String _value = RemoveAlias(ReadValue<String>(_insert_item));
				if (_value == "")
				{
					if (std::find(param_list.begin(), param_list.end(), _insert_item) == param_list.end())
						param_list.push_back(_insert_item);
					_value_list.push_back(_insert_item);
				}
				else
				{
					_value_list.push_back(_value);
				}
			}
			WriteValue("Database.Internal.ParameterList", param_list);
		}
		else if (_table_list.size() > 1)
		{
			ReportError("Insert", Error::INVALID_DATA, "Not Supported! Support inserting data to only single table if any subquery exists, but there are " + std::to_string(_table_list.size()) + " table(s)");
			return false;
		}
		else
		{
			// check if insert item count == select item count
			int _select_count = ReadValue<int>("Database.Internal.LastSelectionCount");
			if (_select_count != _insert_items.size())
			{
				ReportError("Insert", Error::INVALID_DATA, "Element count in clause must be the same as element count for insertion. Count in clause=" + std::to_string(_select_count) + ", but count for insertion=" + std::to_string(_insert_items.size()));
				return false;
			}
		}

		for (auto _model_name : _insert_items)
		{
			if (_model_name.find_first_of(SQL_SYMBOLS) != String::npos)
			{
				ReportError("Insert", Error::INVALID_DATA, "Invalid insert item name: '" + _model_name + "'");
				return false;
			}
		}
		ModifyColumnName(_insert_items);
		for (unsigned int i = 0; i < _quoted_table_list.size(); i++)
		{
			String _table_name = _quoted_table_list[i];
			statement.insert(statement.end(), _statement_prefix.begin(), _statement_prefix.end());
			statement.push_back(_table_name);
			statement.push_back(" (");
			for (unsigned int j = 0; j < _insert_items.size(); j++)
			{
				if (_insert_items[j].find(_table_name) != 0)
					continue;

				// compare the schema with _insert_items
				if (quoted_column_list_.find(_insert_items[j]) == quoted_column_list_.end())
				{
					ReportError("Insert", Error::INVALID_DATA, "Invalid model name:" + _insert_items[j]);
					return false;
				}

				statement.push_back(_insert_items[j]);
				statement.push_back(", ");
			}
			if (_is_values_type)
			{
				_table_name.erase(std::remove(_table_name.begin(), _table_name.end(), '`'), _table_name.end());
				statement.back() = ") VALUES (";
				for (unsigned int j = 0; j < _value_list.size(); j++)
				{
					if (_insert_items[j].find(_quoted_table_list[i]) != 0)
						continue;
					if (_value_list[j].find(_table_name) == 0)
					{
						auto _itr = column_list_.end();
						if ((_itr = column_list_.find(_value_list[j])) != column_list_.end())
						{
							std::replace(_value_list[j].begin(), _value_list[j].end(), '.', '_');
							//if (_itr->second.data_type_name_.find("binary") != String::npos)
							//	param_type_[_value_list[i]] = BINARY;
							//else if (_itr->second.data_type_name_.find("int") != String::npos)
							//	param_type_[param_list[i]] = INT;
						}
						else
						{
							std::replace(_value_list[j].begin(), _value_list[j].end(), '.', '_');
						}
					}
					std::replace(_value_list[j].begin(), _value_list[j].end(), ';', ',');
					statement.push_back(_value_list[j]);
					statement.push_back(", ");
				}
				statement.back() = ")";
				statement.push_back(";\r\n");
			}
			else
			{
				statement.back() = ") ";
				statement.push_back(RemoveAlias(_insert_query));
				statement.push_back(";\r\n");
			}
		}
		statement.pop_back();
		return true;
	}
}