#pragma once
#include "RNA.h"

namespace PythonAgent
{

class PythonAgent : public BioSys::RNA
{
public:
	PUBLIC_API PythonAgent(BioSys::IBiomolecule* owner);
	PUBLIC_API virtual ~PythonAgent();

protected:
	virtual void OnEvent(const DynaArray& name);

private:
	static Mutex mutexPython_;

private:
	String ssystem(const String& command);
	String EscapeJSONString(const String& in, bool escape_quote);
	void EscapeJSON(const String& in, String& out);
	void ReplaceModelNameWithValue(String& target, const String& resolved_model_name = "");
	bool findAndReplaceAll(String& data, const String& toSearch, const String& replaceStr);
	size_t FindRightBracket(const String& target, size_t pos, const Pair<char, char>& bracket);
	void ReplacePathIndex(String& path_str);
};

}