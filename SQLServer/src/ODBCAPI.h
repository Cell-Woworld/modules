#pragma once
#include "nanodbc/nanodbc.h"
#include <string>

namespace Database
{
	class ODBCAPI
	{
		const int CONNECTION_TIMEOUT = 60;		// seconds
	public:
		void connect(const std::string& conn_str)
		{
			connection_.connect(conn_str, CONNECTION_TIMEOUT);
		};

		nanodbc::result execute(const std::string& statement_str, long max_count = 1L)
		{
			nanodbc::statement statement(connection_);
			nanodbc::prepare(statement, NANODBC_TEXT(statement_str));
			return nanodbc::execute(statement, max_count);
		};
	private:
		nanodbc::connection connection_;
	};
}