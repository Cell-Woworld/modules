#pragma once
#include "RNA.h"
#include "../proto/Database.pb.h"
#include "../proto/DatabaseShared.pb.h"
#include <cctype>
#include <iostream>
#include <regex>

static const String SQL_SYMBOLS = "()%+-*/:'\", ;";		// ; for internal use

USING_BIO_NAMESPACE

namespace Database
{

class Database : public BioSys::RNA
{
	static const char EQUAL_UTF8[];
	static const char NOT_EQUAL_UTF8[];
	static const char GREATER_UTF8[];
	static const char LESS_UTF8[];
	static const char GREATER_EQUAL_UTF8[];
	static const char LESS_EQUAL_UTF8[];
	static const char PLUS_UTF8[];
	static const char MINUS_UTF8[];
	static const char PRODUCT_UTF8[];
	static const char DIVIDE_UTF8[];
	static const char DYNAMIC_CONDITION_PREFIX = '#';
public:
	PUBLIC_API Database(BioSys::IBiomolecule* owner);
	PUBLIC_API virtual ~Database();

protected:
	virtual void OnEvent(const DynaArray& name);

private:
	void doQuery();
	void doAddCond();
	void doRemove();
	void doError();
	void doConfig();
	void doUpdate(const Const_DB::PROC_TYPE update_type, const String& update_keyword, const String& update_statement);

	bool is_number(const String& s) {
		return !s.empty() && std::find_if(s.begin(), s.end(), [](char c) { return !std::isdigit(c); }) == s.end();
	};
	String trim(const String& s)
	{
		auto wsfront = std::find_if_not(s.begin(), s.end(), [](int c) {return std::isspace(c); });
		auto wsback = std::find_if_not(s.rbegin(), s.rend(), [](int c) {return std::isspace(c); }).base();
		return (wsback <= wsfront ? std::string() : std::string(wsfront, wsback));
	}
	void RetrieveTableName(const String& parameters, std::unordered_set<String>& table_list);
	String GetParameter(const String& parameter, Set<String>& target_set, const String& prefix = "");
	void Split(const String& input, Array<String>& output, const String& delemiters, const Array<Pair<String, String>>& ignored_pairs = Array<Pair<String, String>>());
	String RemoveAlias(const String& var);
	void ReportError(const String& name, Error::ERROR_CODE code, const String& message);
	size_t FindRightBracket(const String& target, size_t pos, const Pair<String, String>& bracket_pair);
	int GetAncestorDepth(const String& _last_state_parent, const String& _current_state_parent);
	void RemoveQuoteAndUnescaping(String& src);

private:
	BioSys::IBiomolecule* owner_;
};

}