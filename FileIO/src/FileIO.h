#pragma once
#include "RNA.h"
#include <regex>

namespace FileIO
{

class FileIO : public BioSys::RNA
{
public:
	PUBLIC_API FileIO(BioSys::IBiomolecule* owner);
	PUBLIC_API virtual ~FileIO();

protected:
	virtual void OnEvent(const DynaArray& name);

private:
	void RemoveFile(const String& filename);
	int ForEachFile(const std::string& search_path, const std::regex& regex, int depth, std::function<void(const std::string&)> folder_handler, std::function<void(const std::string&)> file_handler);
	void ReplaceStringInFolder(const String& source, const String& target, const String& file_name, const Array<String>& to_be_replaced, const Array<String>& replace_with, bool deep_replacing);
	void ReplaceStringInFile(const String& source, const String& target, const Array<String>& to_be_replaced, const Array<String>& replace_with);

private:
	static Map<String, Mutex> file_io_mutex_;
};

}