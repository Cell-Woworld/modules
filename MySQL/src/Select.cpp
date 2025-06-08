#include "MySQL.h"

namespace Database
{
	bool MySQL::CreateSelectStatement(Array<String>& statement, Array<String>& param_list, bool create_stored_procedure)
	{
		statement = ReadValue<Array<String>>("Database.Internal.Statement");
		Array<String> _query_items = ReadValue<Array<String>>("Database.Internal.Columns");
		Array<String> _table_list = ReadValue<Array<String>>("Database.Internal.TableList");
		Array<String> _cond_list = ReadValue<Array<String>>("Database.Internal.ConditionList");
		Array<String> _filter_list = ReadValue<Array<String>>("Database.Internal.FilterList");
		Array<String> _modifier_list = ReadValue<Array<String>>("Database.Internal.ModifierList");
		bool _column_recovered = false;
		bool _column_alias_removed = false;

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

		if (create_stored_procedure == false && _query_items.size() > 0 && _query_items[0] == "*")
		{
			//ReportError(Error::INVALID_DATA, "Assigning query items dynamically in subquery is not supported");
			//return false;

			// parameters as query items of subquery
			param_list.insert(param_list.end(), _filter_list.begin(), _filter_list.end());
			WriteValue("Database.Internal.ParameterList", param_list);

			for (unsigned int i = 0; i < _filter_list.size(); i++)
				std::replace(_filter_list[i].begin(), _filter_list[i].end(), '.', '_');
			_query_items.erase(_query_items.begin());
			_query_items.insert(_query_items.begin(), _filter_list.begin(), _filter_list.end());
			_filter_list.clear();
			_column_recovered = true;
		}

		for (unsigned int i = 0; i < param_list.size(); i++)
			std::replace(param_list[i].begin(), param_list[i].end(), '.', '_');
		for (auto elem : param_list)
			param_type_.insert(make_pair(elem, STRING));

		//if (_table_list.size() == 0)
		//{
		//	ReportError("Select", Error::INVALID_DATA, "Table names needed. If columns are provided at run-time, please add table names with the \"TABLE\" condition");
		//	return false;
		//}

		Array<String> _quoted_table_list = _table_list;
		ModifyTableName(_quoted_table_list);
		// get schema from DB
		try
		{
			GetAllSchema(_quoted_table_list);
		}
		catch (const std::exception & e)
		{
			ReportError("Select", Error::INVALID_DATA, e.what());
			return false;
		}
		//WriteValue("Database.Internal.ColumnList", Array<String>(_column_list.begin(), _column_list.end()));
		Array<String> _quoted_query_items = _query_items;
		ModifyColumnName(_quoted_query_items);
		for (unsigned int i = 0; i < _query_items.size(); i++)
		{
			std::replace(_quoted_query_items[i].begin(), _quoted_query_items[i].end(), ';', ',');
			if (i < _query_items.size() - 1)
				statement.push_back(_quoted_query_items[i] + ", ");
			else
				statement.push_back(_quoted_query_items[i]);

			auto itr = modelname_function_map_.find(_query_items[i]);
			if (itr != modelname_function_map_.end())
			{
				_column_recovered = true;
				_query_items[i] = itr->second;
			}
			else
			{
				if (_query_items[i].find_first_of(SQL_SYMBOLS + "*") != String::npos)
				{
					continue;
					/*
					if (_query_items[i] == "*" && _filter_list.size() > 0)
					{
						continue;
					}
					else
					{
						ReportError(Error::INVALID_DATA, "Invalid model name:" + _query_items[i]);
						return false;
					}
					*/
				}
				else
				{	// remove alias
					size_t _end_pos = _query_items[i].find_last_of(".");
					_end_pos = _query_items[i].find_first_of(" ", _end_pos + 1);
					if (_end_pos != String::npos && _end_pos != _query_items[i].size() - 1)
					{
						_query_items[i] = _query_items[i].substr(0, _end_pos);
						_column_alias_removed = true;
					}
				}
				_column_recovered = false;
			}
			// compare the schema with _query_items
			if (!_column_recovered && quoted_column_list_.find(_quoted_query_items[i]) == quoted_column_list_.end()
				&& std::count(_query_items[i].begin(), _query_items[i].end(), '.') >= 2)
			{
				ReportError("Select", Error::INVALID_DATA, "Invalid query item name:" + _query_items[i]);
				return false;
			}
		}
		if (_column_recovered || _column_alias_removed)
		{
			WriteValue("Database.Internal.Columns", _query_items);
		}

		if (_quoted_table_list.size() > 0)
		{
			statement.push_back(" FROM ");
			for (unsigned int i = 0; i < _quoted_table_list.size(); i++)
			{
				if (_quoted_table_list[i].find_first_of(SQL_SYMBOLS) != String::npos)
				{	// replace uppercase keyword with variant case keyword
					for (auto _keyword_pair : SQL_KEYWORD)
					{
						_quoted_table_list[i] = std::regex_replace(_quoted_table_list[i], std::regex(_keyword_pair.first), _keyword_pair.second);
					}
				}
				if (i < _quoted_table_list.size() - 1)
					statement.push_back(_quoted_table_list[i] + ", ");
				else
					statement.push_back(_quoted_table_list[i]);
			}
		}

		if (_cond_list.size() > 0)
		{
			ModifyColumnName(_cond_list);
			for (auto _cond = _cond_list.begin(); _cond != _cond_list.end(); ++_cond)
			{
				if (_cond->front() != DYNAMIC_CONDITION_PREFIX)
					continue;
				auto _itr = std::find(param_list.begin(), param_list.end(), *_cond);
				if (_itr != param_list.end())
				{
					// _cond is a string array of conditions
					//_cond = _cond_list.insert(_cond, "(replace(replace(replace(replace(json_extract(");
					//_cond = _cond_list.insert(_cond + 2, ", '$'), '\\\"', '\''), '\"', ''), '[', ''), ']', ''))");
					WriteValue("Database.Internal.DynamicCondition", (String)"IF(" + *_cond  + "='',true," + *_cond + ")");
					//*_cond = "(\",_sub_stmt,\")";
					//*_cond = _cond->substr(1);
				}
			}
			statement.push_back(" WHERE ");
			for (unsigned int i = 0; i < _cond_list.size(); i++)
			{
				if (_cond_list[i].find_first_of(SQL_SYMBOLS) == String::npos)
				{ // compare the schema with _cond_list
					if (quoted_column_list_.find(_cond_list[i]) == quoted_column_list_.end()
						&& std::count(_cond_list[i].begin(), _cond_list[i].end(), '.') >= 2)
					{
						ReportError("Select", Error::INVALID_DATA, "Invalid condition name:" + _cond_list[i]);
						return false;
					}
				}
				else
				{
					size_t _start_pos = _cond_list[i].find("FROM ");
					if (_start_pos != String::npos)
					{
						_start_pos += sizeof("FROM ") - 1;
						size_t _end_pos = _cond_list[i].find("WHERE ");
						String _cond_tables = _cond_list[i].substr(_start_pos, _end_pos - _start_pos - 1);
						String _subquery_items = _cond_list[i].substr(0, _start_pos);
						String _new_cond_tables = "";
						Array<String> _cond_table_list;
						Split(_cond_tables, _cond_table_list, ",");
						for (auto itr = _cond_table_list.begin(); itr != _cond_table_list.end();)
						{
							int j = 0;
							for (; j < _quoted_table_list.size(); j++)
							{
								if (*itr == _quoted_table_list[j])
									break;
							}
							// remove noisy table names from sub-query
							if (j < _quoted_table_list.size() && _subquery_items.find(*itr) == String::npos)
							{
								itr = _cond_table_list.erase(itr);
							}
							else
							{
								_new_cond_tables += *itr + ",";
								++itr;
							}
						}
						if (_new_cond_tables.size() > 0)
						{
							_new_cond_tables.back() = ' ';
							_cond_list[i].replace(_cond_list[i].begin() + _start_pos, _cond_list[i].begin() + _end_pos, _new_cond_tables);
						}
					}
				}
				std::replace(_cond_list[i].begin(), _cond_list[i].end(), ';', ',');
				std::replace(_cond_list[i].begin(), _cond_list[i].end(), '"', '\'');
				statement.push_back(_cond_list[i]);
			}
		}

		Array<String> _modifier_list_ordered[MAX_MODIFIER_ORDER];
		MODIFIER_ORDER _last_order = MAX_MODIFIER_ORDER;
		if (_modifier_list.size() > 0)
		{
			for (unsigned int i = 0; i < _modifier_list.size(); i++)
			{
				std::replace(_modifier_list[i].begin(), _modifier_list[i].end(), ';', ',');
				std::replace(_modifier_list[i].begin(), _modifier_list[i].end(), '"', '\'');

				if (_modifier_list[i] == " HAVING ")
				{
					_modifier_list_ordered[HAVING].push_back(_modifier_list[i]);
					_last_order = HAVING;
				}
				else if (_modifier_list[i] == " GROUP BY ")
				{
					_modifier_list_ordered[GROUP_BY].push_back(_modifier_list[i]);
					_last_order = GROUP_BY;
				}
				else if (_modifier_list[i] == " ORDER BY ")
				{
					_modifier_list_ordered[ORDER_BY].push_back(_modifier_list[i]);
					_last_order = ORDER_BY;
				}
				else if (_modifier_list[i] == " PAGING " || _last_order == PAGING)
				{
					auto itr = format_map::keyword.find(_modifier_list[i]);
					if (itr != format_map::keyword.end())
						_modifier_list[i] = std::regex_replace(_modifier_list[i], std::regex(itr->first), itr->second[format_map::MYSQL]);
					else
					{
						auto _itr_param = param_type_.find(_modifier_list[i]);
						if (_itr_param != param_type_.end())
						{
							_itr_param->second = INT;
						}
					}
					_modifier_list_ordered[PAGING].push_back(_modifier_list[i]);
					_last_order = PAGING;
				}
				else
				{
					assert(_last_order != MAX_MODIFIER_ORDER);
					if (_last_order < MAX_MODIFIER_ORDER)
						_modifier_list_ordered[_last_order].push_back(RemoveAllAlias(_modifier_list[i]));
				}
			}
			// reordering LIMIT and OFFSET
			if (_modifier_list_ordered[PAGING].size() == 4)
			{
				std::swap(_modifier_list_ordered[PAGING][0], _modifier_list_ordered[PAGING][2]);
				std::swap(_modifier_list_ordered[PAGING][1], _modifier_list_ordered[PAGING][3]);
			}
		}

		for (int i = 0; i < MAX_MODIFIER_ORDER; i++)
		{
			statement.insert(statement.end(), _modifier_list_ordered[i].begin(), _modifier_list_ordered[i].end());
		}
		return true;
	}
}