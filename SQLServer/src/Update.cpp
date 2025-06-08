#include "SQLServer.h"
#include <numeric>

namespace Database
{
	bool SQLServer::CreateUpdateStatement(Array<String>& statement, Array<String>& param_list)
	{
		Array<String> _statement_prefix = ReadValue<Array<String>>("Database.Internal.Statement");
		Array<String> _update_items = ReadValue<Array<String>>("Database.Internal.Columns");
		Array<String> _table_list = ReadValue<Array<String>>("Database.Internal.TableList");
		Array<String> _cond_list = ReadValue<Array<String>>("Database.Internal.ConditionList");
		Array<String> _filter_list = ReadValue<Array<String>>("Database.Internal.FilterList");
		Array<String> _modifier_list = ReadValue<Array<String>>("Database.Internal.ModifierList");
		String _update_query = ReadValue<String>("Database.Internal.QueryClause");
		std::unordered_set<String> _table_set(_table_list.begin(), _table_list.end());

		bool _is_values_type = (_update_query == "");

		if (_table_list.size() != 1 && _is_values_type == true)
		{
			ReportError("Update", Error::INVALID_DATA, "Not Supported! Support updating data to only single table, but there are " + std::to_string(_table_list.size()) + " table(s)");
			return false;
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

		Array<String> _quoted_table_list = _table_list;
		ModifyTableName(_quoted_table_list);
		// get schema from DB
		try
		{
			//GetAllSchema(_quoted_table_list);
			GetAllSchema(_table_list);
		}
		catch (const std::exception & e)
		{
			LOG_E(TAG, "SQLServer::CreateUpdateStatement() Execption: %s", e.what());
			ReportError("Update", Error::INVALID_DATA, e.what());
			return false;
		}

		// check duplication of _update_items
		Set<String> _update_set;
		for (auto _update_item : _update_items)
		{
			if (_update_set.find(_update_item) != _update_set.end())
			{
				ReportError("Update", Error::INVALID_DATA, "Duplicate elements in the model list of Database.insert(). name: " + _update_item);
				return false;
			}
			else
			{
				_update_set.insert(_update_item);
			}
		}
		for (auto _model_name : _update_items)
		{
			if (_model_name.find_first_of(SQL_SYMBOLS) != String::npos)
			{
				ReportError("Update", Error::INVALID_DATA, "Invalid model name: '" + _model_name + "'");
				return false;
			}
		}

		if (_is_values_type == true)
		{
			//param_list.insert(param_list.begin(), _update_items.begin(), _update_items.end());
			Array<String> _value_list;
			for (auto _update_item : _update_items)
			{
				String _value = RemoveAlias(ReadValue<String>(_update_item));
				if (_value == "")
				{
					param_list.push_back(_update_item);
					_value_list.push_back(_update_item);
				}
				else
				{
					_value_list.push_back(_value);
				}
			}
			WriteValue("Database.Internal.ParameterList", param_list);

			Array<String> _quoted_update_items = _update_items;
			ModifyColumnName(_quoted_update_items);

			const String& _table_name = _quoted_table_list[0];
			statement.insert(statement.end(), _statement_prefix.begin(), _statement_prefix.end());
			statement.push_back(_table_name);
			statement.push_back(" SET ");
			int _param_index = 0;
			for (unsigned int i = 0; i < _quoted_update_items.size(); i++)
			{
				if (_quoted_update_items[i].find(_table_name) != 0)
					continue;

				// compare the schema with _update_items
				if (quoted_column_list_.find(_quoted_update_items[i]) == quoted_column_list_.end())
				{
					ReportError("Update", Error::INVALID_DATA, "Invalid model name:" + _quoted_update_items[i]);
					return false;
				}

				statement.push_back(_quoted_update_items[i]);
				statement.push_back("=");

				auto _itr = column_list_.end();
				if ((_itr = column_list_.find(_value_list[_param_index])) != column_list_.end())
				{
					std::replace(_value_list[_param_index].begin(), _value_list[_param_index].end(), '.', '_');
					//if (_itr->second.data_type_name_.find("binary") != String::npos)
					//	param_type_[param_list[_param_index]] = BINARY;
					//else if (_itr->second.data_type_name_.find("int") != String::npos)
					//	param_type_[param_list[i]] = INT;
				}
				else
				{
					//std::replace(_value_list[_param_index].begin(), _value_list[_param_index].end(), '.', '_');
				}
				std::replace(_value_list[_param_index].begin(), _value_list[_param_index].end(), ';', ',');
				statement.push_back("@" + _value_list[_param_index] );
				statement.push_back(", ");
				_param_index++;
			}
			statement.back() = " ";

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
		}
		else
		{
			// check if update item count == select item count
			int _select_count = ReadValue<int>("Database.Internal.LastSelectionCount");
			if (_select_count < _update_items.size())
			{
				ReportError("Update", Error::INVALID_DATA, "Element count in clause must be greater than or equal to element count for updating. Count in clause=" + std::to_string(_select_count) + ", but count for updating=" + std::to_string(_update_items.size()));
				return false;
			}
			statement.insert(statement.end(), _statement_prefix.begin(), _statement_prefix.end());

			// new approach modified at 2020/04/06
			size_t _table_start_pos = _update_query.find(" FROM ") + sizeof(" FROM ") - 1;
			size_t _queryitems_start_pos = _update_query.find("SELECT ") + sizeof("SELECT ") - 1;
			size_t _queryitems_end_pos = _table_start_pos - sizeof(" FROM ") + 1;
			Array<String> _queryitems_list;
			Split(_update_query.substr(_queryitems_start_pos, _queryitems_end_pos - _queryitems_start_pos), _queryitems_list, ",", { std::make_pair("'", "'"),std::make_pair("(", ")") });

			std::replace(_update_query.begin(), _update_query.end(), ';', ',');
			_table_set.insert(_update_query);
			_table_list = Array<String>(_table_set.begin(), _table_set.end());
			WriteValue("Database.Internal.TableList", _table_list);

			_quoted_table_list = _table_list;
			ModifyTableName(_quoted_table_list);
			for (auto _table_name : _quoted_table_list)
			{
				statement.push_back(_table_name);
				statement.push_back(", ");
			}

			String _subquery_alias = GetAlias(_update_query);

			statement.back() = " SET ";

			ModifyColumnName(_update_items);
			for (unsigned int i = 0; i < _update_items.size(); i++)
			{
				if (_update_items[i] != "null")
				{
					std::replace(_queryitems_list[i].begin(), _queryitems_list[i].end(), ';', ',');
					String _subquery_elem = GetAlias(_queryitems_list[i]);
					if (_subquery_elem != "")
						_subquery_elem = _subquery_alias + "." + _subquery_elem;
					else
					{
						size_t _pos = _queryitems_list[i].find_last_of('.');
						if (_pos != String::npos)
							_subquery_elem = _subquery_alias + _queryitems_list[i].substr(_pos);
						else
							_subquery_elem = _queryitems_list[i];
					}
					statement.push_back(_update_items[i] + "=" + _subquery_elem);
					if (i < _update_items.size() - 1)
						statement.push_back(", ");
				}
				else
				{
					if (i == _update_items.size() - 1 && statement.back() == ", ")
						statement.pop_back();
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
		}

		Array<String> _modifier_list_ordered[MAX_MODIFIER_ORDER];
		if (_modifier_list.size() > 0)
		{
			if (_modifier_list[0] == " HAVING ")
			{
				_modifier_list_ordered[HAVING] = _modifier_list;
			}
			else if (_modifier_list[0] == " GROUP BY ")
			{
				_modifier_list_ordered[GROUP_BY] = _modifier_list;
			}
			else if (_modifier_list[0] == " ORDER BY ")
			{
				_modifier_list_ordered[ORDER_BY] = _modifier_list;
			}
			else if (_modifier_list[0] == " PAGING ")
			{
				for (unsigned int i = 0; i < _modifier_list.size(); i++)
				{
					auto itr = format_map::keyword.find(_modifier_list[i]);
					if (itr != format_map::keyword.end())
						_modifier_list[i] = std::regex_replace(_modifier_list[i], std::regex(itr->first), itr->second[format_map::MSSQL]);
					else
					{
						auto _itr_param = param_type_.find(_modifier_list[i]);
						if (_itr_param != param_type_.end())
						{
							_itr_param->second = INT;
						}
					}
				}
				_modifier_list_ordered[PAGING] = _modifier_list;
			}
		}

		for (int i = 0; i < MAX_MODIFIER_ORDER; i++)
		{
			statement.insert(statement.end(), _modifier_list_ordered[i].begin(), _modifier_list_ordered[i].end());
		}
		return true;
	}

	void SQLServer::Split(const String& input, Array<String>& output, const String& delemiters, const Array<Pair<String, String>>& ignored_pairs)
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
						size_t _ignored_end = input.find_first_of(_ignored_pair.second, _ignored_start + 1);
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
}