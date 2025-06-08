#include "MySQL.h"
#include <numeric>

namespace Database
{
	bool MySQL::CreateUpdateStatement(Array<String>& statement, Array<String>& param_list)
	{
		//"\tDECLARE _value VARCHAR(" + MAX_VCHAR_PARAM_SIZE + ");\r\n"
		static const String _dynamic_update_template = {
			"BEGIN\r\n"
			"\tDECLARE _stmt TEXT(65535) DEFAULT '';\r\n"
			"\tDECLARE _idx INTEGER;\r\n"
			"\tDECLARE _count INTEGER;\r\n"
			"\tDECLARE _total INTEGER;\r\n"
			"\tDECLARE _name VARCHAR(" + MAX_MODEL_NAME_SIZE + ");\r\n"
			"\tDECLARE _value TEXT;\r\n"
			"\tSET _stmt = CONCAT(_stmt, 'UPDATE %s SET ');\r\n"			// %UPDATE_TABLE_NAME = 'UPDATE %TABLE_NAME SET '
			"\tSET _idx = 0;\r\n"
			"\tSET _count = 0;\r\n"
			"\tSET _total = 0;\r\n"
			"\tDROP TEMPORARY TABLE IF EXISTS array_table;\r\n"
			"\tCREATE TEMPORARY TABLE array_table (idx INT, name VARCHAR(" + MAX_MODEL_NAME_SIZE + "), value TEXT);\r\n" //VARCHAR(" + MAX_VCHAR_PARAM_SIZE + "));\r\n"
			"\tINSERT INTO array_table (idx, name, value) VALUES %s;\r\n"		// %KEY_VALUE_PAIRS from schema and parameters ex.(0,'col2',tbl1_col2),(1,'col3',tbl1_col3)
			"\tSELECT COUNT(*) into _total FROM array_table;\r\n"
			"\tWHILE _idx < _total DO\r\n"
				"\t\tSELECT name, value into _name, _value FROM array_table WHERE _idx = idx;\r\n" // and value IS NOT NULL;"
				"\t\tif (_value IS NOT NULL) then\r\n"
					"\t\t\tif (_count > 0) then\r\n"
					  "\t\t\t\tset _stmt = CONCAT(_stmt, \", \", _name, \"='\", _value, \"'\");\r\n"
					"\t\t\telse\r\n"
					  "\t\t\t\tset _stmt = CONCAT(_stmt, _name, \"='\", _value, \"'\");\r\n"
					"\t\t\tend if;\r\n"
					"\t\t\tSET _count = _count + 1;\r\n"
				"\t\tend if;\r\n"
				"\t\tSET _idx = _idx + 1;\r\n"
			"\tEND WHILE;\r\n"
			//"\tSET _stmt = CONCAT(_stmt, \" WHERE \", %s, \";\");\r\n"				// %CONDITIONS, ex. db1.tbl1.col1=tbl1_col1
			"%s\r\n"
			"\tSET @_stmt=_stmt;\r\n"
			"\tPREPARE update_stmt FROM @_stmt;\r\n"
			"\tEXECUTE update_stmt;\r\n"
			"\tDROP TABLE array_table;\r\n"
			"\tDEALLOCATE PREPARE update_stmt;\r\n"
			"END"
		};
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
			LOG_W(TAG, "Support updating data to only single table, but size of TableList = %zd", _table_list.size());
			//ReportError("Update", Error::INVALID_DATA, "Not Supported! Support updating data to only single table, but there are " + std::to_string(_table_list.size()) + " table(s)");
			//return false;
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
			GetAllSchema(_quoted_table_list);
		}
		catch (const std::exception & e)
		{
			LOG_E(TAG, "MySQL::CreateUpdateStatement() Execption: %s", e.what());
			ReportError("Update", Error::INVALID_DATA, e.what());
			return false;
		}

		if (_update_items.size() == 1 && _update_items[0] == "*")
		{
			if (_is_values_type == false)
			{
				ReportError("Update", Error::INVALID_DATA, "Assigning update items dynamically with subquery is not supported");
				return false;
			}

			Array<String> _quoted_update_items;
			_update_items.clear();
			for (auto _column : column_list_)
			{
				//String _column_name = _column.first;
				//_quoted_update_items.push_back(_column_name);
				//_column_name.erase(std::remove(_column_name.begin(), _column_name.end(), '`'), _column_name.end());
				//_update_items.push_back(_column_name);
				_update_items.push_back(_column.first);
			}
			WriteValue("Database.Internal.Columns", _update_items);

			param_list.insert(param_list.begin(), _update_items.begin(), _update_items.end());
			WriteValue("Database.Internal.ParameterList", param_list);

			//ModifyColumnName(_update_items);

			for (auto _model_name : _update_items)
			{
				if (_model_name.find_first_of(SQL_SYMBOLS) != String::npos)
				{
					ReportError("Update", Error::INVALID_DATA, "Invalid model name: '" + _model_name + "'");
					return false;
				}
			}

			const String& _table_name = _table_list[0];

			Array<String> _key_value_pairs;
			int _param_index = 0;
			for (unsigned int i = 0; i < _update_items.size(); i++)
			{
				if (_update_items[i].find(_table_name) != 0)
					continue;

				// compare the schema with _update_items
				if (quoted_column_list_.find(_quoted_update_items[i]) == quoted_column_list_.end())
				{
					ReportError("Update", Error::INVALID_DATA, "Invalid update item name:" + _update_items[i]);
					return false;
				}

				_key_value_pairs.push_back("(");
				_key_value_pairs.push_back(std::to_string(_param_index));
				_key_value_pairs.push_back(",");
				_key_value_pairs.push_back("'");
				_key_value_pairs.push_back(_quoted_update_items[i]);
				_key_value_pairs.push_back("'");
				_key_value_pairs.push_back(",");
				std::replace(param_list[_param_index].begin(), param_list[_param_index].end(), '.', '_');
				_key_value_pairs.push_back(param_list[_param_index]);
				_key_value_pairs.push_back(")");
				_key_value_pairs.push_back(",");
				_param_index++;
			}
			_key_value_pairs.pop_back();

			String _key_values = std::accumulate(_key_value_pairs.begin(), _key_value_pairs.end(), String());
			String _conditions = "";
			if (_cond_list.size() > 0)
			{
				ModifyColumnName(_cond_list);
				for (unsigned int i = 0; i < _cond_list.size();)
				{
					// compare the schema with _cond_list
					if (quoted_column_list_.find(_cond_list[i]) == quoted_column_list_.end()
						&& std::count(_cond_list[i].begin(), _cond_list[i].end(), '.') >= 2)
					{
						ReportError("Update", Error::INVALID_DATA, "Invalid condition name:" + _cond_list[i]);
						return false;
					}

					if (i > 0)
						_conditions += ", \"";
					else
						_conditions += "\"";
					size_t _split_pos = _cond_list[i].find_last_of(' ');
					if (_split_pos != String::npos)
					{
						_conditions += _cond_list[i].substr(0, _split_pos) + " '\", " + _cond_list[i].substr(_split_pos + 1) + ", \"'\"";
						i++;
					}
					else
					{
						_conditions += _cond_list[i] + _cond_list[i + 1] + "'\", " + _cond_list[i + 2] + ", \"'\"";
						i += 3;
					}
				}
				_conditions = string_format("\tSET _stmt = CONCAT(_stmt, \" WHERE \", %s, \";\");", _conditions.c_str());
			}

			statement.push_back(string_format(_dynamic_update_template, _table_name.c_str(), _key_values.c_str(), _conditions.c_str()));
		}
		else
		{
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
					ReportError("Update", Error::INVALID_DATA, "Invalid model name: '" + _model_name + "'. If you want to use functions here, please use \"target_model_name\" to convert it into a model name.");
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

				ModifyColumnName(_update_items);

				statement.insert(statement.end(), _statement_prefix.begin(), _statement_prefix.end());
				const String& _table_name = _quoted_table_list[0];
				statement.push_back(_table_name);
				for (int i = 1; i < _quoted_table_list.size(); i++)
				{
					statement.push_back(", " + _quoted_table_list[i]);
				}
				statement.push_back(" SET ");
				int _param_index = 0;
				for (unsigned int i = 0; i < _update_items.size(); i++)
				{
					bool _found = false;
					for (unsigned int j = 0; j < _quoted_table_list.size(); j++)
					{
						if (_update_items[i].find(_quoted_table_list[j]) == 0)
						{
							_found = true;
							break;
						}
					}
					if (!_found)
						continue;

					// compare the schema with _update_items
					if (quoted_column_list_.find(_update_items[i]) == quoted_column_list_.end())
					{
						ReportError("Update", Error::INVALID_DATA, "Invalid model name:" + _update_items[i]);
						return false;
					}

					statement.push_back(_update_items[i]);
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
					statement.push_back(_value_list[_param_index]);
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

				/* not work for two tables with the same table name
				// rearrange _update_query
				_update_query = RemoveAlias(_update_query);
				_update_query = _update_query.substr(1, _update_query.size() - 2);		// remove "()"
				size_t _table_start_pos = _update_query.find(" FROM ") + sizeof(" FROM ") - 1;
				size_t _table_end_pos = _update_query.find(" WHERE ");

				Array<String> _table_list_in_subquery;
				if (_table_end_pos != String::npos)
				{
					if (_table_end_pos - _table_start_pos > 0)
					{
						Split(_update_query.substr(_table_start_pos, _table_end_pos - _table_start_pos), _table_list_in_subquery, ",", { std::make_pair("(", ")") });
					}
				}
				else
				{
					Split(_update_query.substr(_table_start_pos), _table_list_in_subquery, ",", { std::make_pair("(", ")") });
					//statement.push_back(", ");
					//statement.push_back(_update_query.substr(_table_start_pos));
				}
				for (auto _table_name : _table_list_in_subquery)
				{
					if (_table_name.find_first_of(SQL_SYMBOLS) == String::npos)
						_table_name.erase(std::remove(_table_name.begin(), _table_name.end(), '`'), _table_name.end());
					_table_set.insert(_table_name);
				}
				_table_list = Array<String>(_table_set.begin(), _table_set.end());
				WriteValue("Database.Internal.TableList", _table_list);

				_quoted_table_list = _table_list;
				ModifyTableName(_quoted_table_list);
				for (auto _table_name : _quoted_table_list)
				{
					statement.push_back(_table_name);
					statement.push_back(", ");
				}

				statement.back() = " SET ";
				size_t _queryitems_start_pos = _update_query.find("SELECT ") + sizeof("SELECT ") - 1;
				size_t _queryitems_end_pos = _table_start_pos - sizeof(" FROM ") + 1;
				Array<String> _queryitems_list;
				Split(_update_query.substr(_queryitems_start_pos, _queryitems_end_pos - _queryitems_start_pos), _queryitems_list, ",", { std::make_pair("'", "'"),std::make_pair("(", ")") });

				// checked already by "if (_select_count != _update_items.size())"
				//if (_update_items.size() != _queryitems_list.size())
				//{
				//	ReportError(Error::INVALID_DATA, "count of update items(" + std::to_string(_update_items.size()) + ") != count of export items in subquery(" + std::to_string(_queryitems_list.size()) + ")");
				//	return false;
				//}

				ModifyColumnName(_update_items);
				for (unsigned int i = 0; i < _update_items.size(); i++)
				{
					std::replace(_queryitems_list[i].begin(), _queryitems_list[i].end(), ';', ',');
					statement.push_back(_update_items[i] + "=" + RemoveAlias(_queryitems_list[i]));
					if (i < _update_items.size() - 1)
						statement.push_back(", ");
				}
				if (_table_end_pos != String::npos)
				{	// where conditions
					statement.push_back(_update_query.substr(_table_end_pos));
				}

				if (_cond_list.size() > 0)
				{
					ModifyColumnName(_cond_list);
					if (_table_end_pos == String::npos)
						statement.push_back(" WHERE ");
					else
						statement.push_back(" AND ");
					for (unsigned int i = 0; i < _cond_list.size(); i++)
					{
						std::replace(_cond_list[i].begin(), _cond_list[i].end(), ';', ',');
						statement.push_back(_cond_list[i]);
					}
				}
				*/
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
						_modifier_list_ordered[_last_order].push_back(_modifier_list[i]);
				}
			}
			// reordering LIMIT and OFFSET, but ignore OFFSET (not supported by UPDATE)
			if (_modifier_list_ordered[PAGING].size() == 4)
			{
				std::swap(_modifier_list_ordered[PAGING][0], _modifier_list_ordered[PAGING][2]);
				std::swap(_modifier_list_ordered[PAGING][1], _modifier_list_ordered[PAGING][3]);
				_modifier_list_ordered[PAGING].pop_back();
				_modifier_list_ordered[PAGING].pop_back();
			}
		}

		for (int i = 0; i < MAX_MODIFIER_ORDER; i++)
		{
			statement.insert(statement.end(), _modifier_list_ordered[i].begin(), _modifier_list_ordered[i].end());
		}
		return true;
	}

	void MySQL::Split(const String& input, Array<String>& output, const String& delemiters, const Array<Pair<String, String>>& ignored_pairs)
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