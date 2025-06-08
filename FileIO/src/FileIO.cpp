#include "FileIO.h"
#include "../proto/FileIO.pb.h"
#include <filesystem>
#include <codecvt>
#include "uuid.h"
#include "glob.hpp"
#include <fstream>

#define TAG "FileIO"

namespace FileIO
{

#ifdef STATIC_API
	extern "C" PUBLIC_API  BioSys::RNA * FileIO_CreateInstance(BioSys::IBiomolecule * owner)
	{
		return new FileIO(owner);
	}
#else
	extern "C" PUBLIC_API BioSys::RNA* CreateInstance(BioSys::IBiomolecule* owner)
	{
		return new FileIO(owner);
	}
#endif

	Map<String, Mutex> FileIO::file_io_mutex_;

	FileIO::FileIO(BioSys::IBiomolecule* owner)
		:RNA(owner, "FileIO", this)
	{
		init();
	}

	FileIO::~FileIO()
	{
	}

	void FileIO::OnEvent(const DynaArray& name)
	{
		USING_BIO_NAMESPACE
		namespace fs = std::filesystem;
		const String& _name = name.str();
		switch (hash(_name))
		{
		case "FileIO.Read"_hash:
		{
			String _filename = ReadValue<String>(_name + ".filename");
			if (ReadValue<bool>(_name + ".utf8Name"))
				setlocale(LC_ALL, ".65001");
			Map<String, String> _file_map;
			bool _is_temp_file = true;
			MutexLocker _lock(file_io_mutex_[_filename]);
			FILE* _file = nullptr;
			String _target = ReadValue<String>(_name + ".target");
			if (_target == "")
				_target = String("return.") + _name;
			else
			{
				_is_temp_file = false;
				_file_map = ReadValue<Map<String, String>>("FileIO.Read.Map");
				//_file_map.insert(std::make_pair(_target, _filename));
				//_file_map[_target] = _filename;
			}
			try
			{
				_file = fopen((GetRootPath() + _filename).c_str(), "rb");
				if (_file == nullptr)
				{
					_file = fopen(_filename.c_str(), "rb");
					if (!_is_temp_file)
					{
						fs::path _filepath(_filename);
						_file_map[_target] = fs::absolute(_filepath).string();
					}
				}
				else
				{
					if (!_is_temp_file)
					{
						fs::path _filepath(GetRootPath() + _filename);
						_file_map[_target] = fs::absolute(_filepath).string();
					}
				}
				if (_file != nullptr)
				{
					fseek(_file, 0, SEEK_END);
					size_t _size = ftell(_file);
					fseek(_file, 0, SEEK_SET);
					String _buf(_size, 0);
					_size = fread((void*)_buf.data(), sizeof(unsigned char), _size, _file);
					fclose(_file);
					WriteValue(_target, _buf);
					WriteValue(_target + ".size", (unsigned long long)_buf.size());
					if (!_is_temp_file)
						WriteValue("FileIO.Read.Map", _file_map);
				}
				else
				{
					LOG_W(TAG, "fail to open file \"%s\"", _filename.c_str());
					WriteValue(_target, "");
					WriteValue(_target + ".size", 0);
				}
			}
			catch (const std::exception & e)
			{
				LOG_W(TAG, "exception (%s) thrown by fopen/fread(%s)", e.what(), _filename.c_str());
				WriteValue(_target, "");
				WriteValue(_target + ".size", 0);
			}
			Remove(_name + ".target");
			break;
		}
		case "FileIO.Write"_hash:
		{
			String _filename = ReadValue<String>(_name + ".filename");
			if (ReadValue<bool>(_name + ".utf8Name"))
				setlocale(LC_ALL, ".65001");
			MutexLocker _lock(file_io_mutex_[_filename]);
			String _content = ReadValue<String>(_name + ".content");
			bool _force = ReadValue<bool>(_name + ".force");
			if (_content == "")
			{
				std::cout << "****** Action name: " << _name << ", filename: " << _filename << ", content is empty ******" << "\n";
				break;
			}
			try
			{
				FILE* _file = fopen((GetRootPath() + _filename).c_str(), "wb");
				if (_file == nullptr)
				{
					if (_force)
					{
						String _folder = (GetRootPath() + _filename);
						size_t _pos= _folder.find_last_of("/\\");
						if (_pos != String::npos)
						{
							_folder = _folder.substr(0, _pos);
							if (fs::create_directories(_folder) == false)
							{
								_pos = _filename.find_last_of("/\\");
								if (_pos != String::npos)
								{
									_folder = _filename.substr(0, _pos);
									fs::create_directories(_folder);
								}
							}
						}
					}
					_file = fopen((GetRootPath() + _filename).c_str(), "wb");
					if (_file == nullptr)
						_file = fopen(_filename.c_str(), "wb");
				}
				if (_file != nullptr)
				{
					size_t _size = fwrite((void*)_content.data(), sizeof(unsigned char), _content.size(), _file);
					fclose(_file);
				}
			}
			catch (const std::exception & e)
			{
				LOG_W(TAG, "exception (%s) thrown by fopen/fwrite(%s)", e.what(), _filename.c_str());
			}
			break;
		}
		case "FileIO.Remove"_hash:
		{
			String _filename = ReadValue<String>(_name + ".filename");
			if (ReadValue<bool>(_name + ".utf8Name"))
				setlocale(LC_ALL, ".65001");
			if (_filename != "")
			{
				RemoveFile(_filename);
			}
			else
			{
				Array<String> _file_list = ReadValue<Array<String>>(_name + ".fileList");
				for (auto& _filename : _file_list)
				{
					//if (_filename.front() == _filename.back() && (_filename.front() == '\"' || _filename.front() == '\''))
					if (_filename.front() == _filename.back() && _filename.front() == '\"')
					{
						RemoveFile(_filename.substr(1, _filename.size() - 2));
					}
					else
					{
						RemoveFile(_filename);
					}
				}
			}
			break;
		}
		case "FileIO.FileExists"_hash:
		{
			String _filename = ReadValue<String>(_name + ".filename");
			if (ReadValue<bool>(_name + ".utf8Name"))
				setlocale(LC_ALL, ".65001");
			MutexLocker _lock(file_io_mutex_[_filename]);
			WriteValue((String)"return." + _name, fs::exists(GetRootPath() + _filename));
#ifdef _DEBUG
			if (!fs::exists(GetRootPath() + _filename))
			{
				LOG_D(TAG, "fs::exists() File \"%s\" not found", (GetRootPath() + _filename).c_str());
			}
#endif
			break;
		}
		case "FileIO.Print"_hash:
		{
			bool _net_printer = ReadValue<bool>(_name + ".netPrinter");
			String _printer_name = ReadValue<String>(_name + ".printerName");
			String _port_name = ReadValue<String>(_name + ".portName");
			String _filename = ReadValue<String>(_name + ".filename");
			String _content = ReadValue<String>(_name + ".content");
			bool _remove_output_file = ReadValue<bool>(_name + ".removeOutputFile");
			int _waitTime = ReadValue<int>(_name + ".waitTime");
			if (_printer_name == "")
				_printer_name = "\\\\localhost\\DefaultPrinter";
			if (_port_name == "")
				_port_name = "LPT1";
			if (_waitTime == 0)
				_waitTime = 30;
			if (_filename != "")
			{
				namespace fs = std::filesystem;
				if (fs::exists(GetRootPath() + _filename))
					_filename = GetRootPath() + _filename;
#ifdef _WIN32
				if (_net_printer == true)
					system((String("net use ") + _port_name + ": \"" + _printer_name + "\"").c_str());
				system((String("print /D:") + _port_name + " " + std::regex_replace(_filename, std::regex("/"), "\\")).c_str());
				std::this_thread::sleep_for(std::chrono::seconds(_waitTime));
				if (_net_printer == true)
					system((String("net use ") + _port_name + ": /delete /y").c_str());
#else
				LOG_E(TAG, "printer has not been supported yet.");
#endif
			}
			else if (_content != "")
			{
				_filename = GetRootPath() + uuids::to_string(uuids::uuid_system_generator()());
				FILE* _file = nullptr;
				try
				{
					FILE* _file = fopen(_filename.c_str(), "wt, ccs=UTF-16LE");
					if (_file != nullptr)
					{
						using namespace std;
						_content = regex_replace(_content, std::regex("\r\n"), "\n");
						//wstring_convert<codecvt_utf8<char16_t, 0x10ffff, codecvt_mode(generate_header | little_endian)>, char16_t> convert_le;
						//u16string _unicode_content = convert_le.from_bytes(_content);
						//size_t _size = fwrite((void*)_unicode_content.data(), sizeof(char16_t), _unicode_content.size(), _file);
						std::wstring_convert<std::codecvt_utf8<wchar_t>> Conver_UTF8;
						WString _unicode_content = Conver_UTF8.from_bytes(_content);
						size_t _size = fwrite((void*)_unicode_content.data(), sizeof(wchar_t), _unicode_content.size(), _file);
						//size_t _size = fwrite((void*)_content.data(), sizeof(char), _content.size(), _file);
						fclose(_file);
						_file = nullptr;
#ifdef _WIN32
						if (_net_printer == true)
							system((String("net use ") + _port_name + ": \"" + _printer_name + "\"").c_str());
						system((String("print /D:") + _port_name + " " + std::regex_replace(_filename, std::regex("/"), "\\")).c_str());
						std::this_thread::sleep_for(std::chrono::seconds(_waitTime));
						if (_net_printer == true)
							system((String("net use ") + _port_name + ": /delete /y").c_str());
#else
						LOG_E(TAG, "printer has not been supported yet.");
#endif
						if (_remove_output_file)
							RemoveFile(_filename);
					}
				}
				catch (const std::exception& e)
				{
					if (_file != nullptr)
					{
						fclose(_file);
						_file = nullptr;
						RemoveFile(_filename);
					}
					LOG_W(TAG, "exception (%s) thrown by Print(%s)", e.what(), _filename.c_str());
				}
			}
			else
			{
				LOG_E(TAG, "Both filename and content are empty while Print");
			}
			break;
		}
		case "FileIO.Copy"_hash:
		{
			if (ReadValue<bool>(_name + ".utf8Name"))
				setlocale(LC_ALL, ".65001");
			String _source = GetRootPath() + ReadValue<String>(_name + ".source");
			String _target = GetRootPath() + ReadValue<String>(_name + ".target");
			bool _deep_copy = ReadValue<bool>(_name + ".deepCopy");
			bool _create_symlinks = ReadValue<bool>(_name + ".createSymlinks");
			if (_source == "" || _target == "")
			{
				LOG_E(TAG, "Action name: %s, both source: \"%s\"and target: \"%s\" cannot be empty", _name.c_str(), _source.c_str(), _target.c_str());
				break;
			}
			try
			{
				bool _folder_exists = false;
				if (_source.find_first_of("?*") != String::npos)
				{
					if ((_folder_exists = fs::exists(_target)) == false)
					{
						_folder_exists = fs::create_directories(_target);
					}

					const std::regex star_replace("\\*");
					const std::regex questionmark_replace("\\?");
					size_t _pos = _source.find_last_of("/\\");
					//std::filesystem::directory_iterator _iter_src{ _source.substr(0, _pos) };
					String _folder_name = _source.substr(0, _pos);
					String _file_name = _source.substr(_pos + 1);
					if (fs::exists(_folder_name))
					{
						auto wildcard_pattern = std::regex_replace(
							//std::regex_replace(_iter_src->path().filename().string(), star_replace, ".*"),
							std::regex_replace(_file_name, star_replace, ".*"),
							questionmark_replace, ".");
						size_t _parent_path_size = _folder_name.size();
						int n = 0;
						ForEachFile(
							_folder_name,
							std::regex(wildcard_pattern),
							1,
							[&](const std::string& folder_name)
							{
								++n;
								LOG_D(TAG, "folder(%d): %s\n", n, folder_name.c_str());
								const String _conjection = (_target.back() == '/' || _target.back() == '\\') ? "" : "/";
								const String _relative_folder_name = folder_name.substr(_parent_path_size);
								//if (fs::exists(_target + _conjection + _relative_folder_name) == false)
								//{
								//	fs::create_directories(_target + _conjection + _relative_folder_name);
								//}
								auto copyOptions = fs::copy_options::overwrite_existing;
								//if (_deep_copy)
								//	copyOptions |= fs::copy_options::recursive;
								if (_create_symlinks)
									copyOptions |= fs::copy_options::create_symlinks;
								fs::copy(folder_name, _target + _conjection + _relative_folder_name, copyOptions);
							},
							[&](const std::string& file_name) {
								++n; 
								LOG_D(TAG, "file(%d): %s\n", n, file_name.c_str());
								const String _conjection = (_target.back() == '/' || _target.back() == '\\') ? "" : "/";
								const String _relative_file_name = file_name.substr(_parent_path_size);
								auto copyOptions = fs::copy_options::overwrite_existing;
								//if (_deep_copy)
								//	copyOptions |= fs::copy_options::recursive;
								if (_create_symlinks)
									copyOptions |= fs::copy_options::create_symlinks;
								fs::copy(file_name, _target + _conjection + _relative_file_name, copyOptions);
							}
						);
					}
				}
				else
				{
					if (fs::exists(_source) == true && (_folder_exists = fs::exists(_target)) == false)
					{
						if (fs::is_directory(_source))
						{
							if (_create_symlinks == true)
							{
								size_t _pos = String::npos;
								if (_target.back() != '/' && _target.back() != '\\')
								{
									_pos = _target.find_last_of("/\\");
								}
								else
								{
									_pos = _target.find_last_of("/\\", _target.size() - 2);
								}
								fs::create_directories(_target.substr(0, _pos));
								fs::create_directory_symlink(_source, _target);
								LOG_D(TAG, "create symbol link %s to %s", _target.c_str(), _source.c_str());
								break;
							}
							else
							{
								_folder_exists = fs::create_directories(_target);
							}
						}
						else
						{
							size_t _pos = _target.find_last_of("/\\");
							if ((_folder_exists = fs::exists(_target.substr(0, _pos))) == false)
							{
								_folder_exists = fs::create_directories(_target.substr(0, _pos));
							}
						}
					}
					if (_folder_exists && !(fs::is_directory(_source) && _create_symlinks))
					{
						auto copyOptions = fs::copy_options::overwrite_existing;
						if (_deep_copy)
							copyOptions |= fs::copy_options::recursive;
						if (_create_symlinks)
							copyOptions |= fs::copy_options::create_symlinks;
						fs::copy(_source, _target, copyOptions);
					}
				}
			}
			catch (const std::exception& e)
			{
				LOG_W(TAG, "exception (%s) thrown by copy from \"%s\" to \"%s\"", e.what(), _source.c_str(), _target.c_str());
			}
			break;
		}
		case "FileIO.ReplaceStringInFiles"_hash:
		{
			String _source = GetRootPath() + ReadValue<String>(_name + ".source");
			String _target = GetRootPath() + ReadValue<String>(_name + ".target");
			String _filter = ReadValue<String>(_name + ".filter");
			Array<String> _to_be_replaced = ReadValue<Array<String>>(_name + ".to_be_replaced");
			Array<String> _replace_with = ReadValue<Array<String>>(_name + ".replace_with");
			bool _deep_replace = ReadValue<bool>(_name + ".deepReplacing");
			if (ReadValue<bool>(_name + ".utf8Name"))
				setlocale(LC_ALL, ".65001");
			if (fs::exists(_source))
				ReplaceStringInFolder(_source, _target, _filter, _to_be_replaced, _replace_with, _deep_replace);
			break;
		}
		default:
			break;
		}
	}

	void FileIO::RemoveFile(const String& filename)
	{
		namespace fs = std::filesystem;
		MutexLocker _lock(file_io_mutex_[filename]);
		try
		{
			if (fs::is_directory(GetRootPath() + filename))
				fs::remove_all(GetRootPath() + filename);
			else if (fs::is_directory(filename))
				fs::remove_all(filename);
			else if (filename.find('*') != String::npos)
			{
				int _file_count = 0;
				// Match on a single pattern
				for (const auto& p : glob::glob(GetRootPath() + filename))
				{
					if (std::filesystem::remove(p.string()) == true)
						_file_count++;
				}
				if (_file_count == 0)
				{
					for (const auto& p : glob::glob(filename))
					{
						if (std::filesystem::remove(p.string()) == true)
							_file_count++;
					}
				}
				if (_file_count == 0)
				{
					LOG_W(TAG, "fs::remove() File \"%s\" or \"%s\" not found", (GetRootPath() + filename).c_str(), filename.c_str());
				}
			}
			else if (fs::remove(GetRootPath() + filename) == false)
			{
				if (fs::remove(filename) == false)
					LOG_W(TAG, "fs::remove() File \"%s\" or \"%s\" not found", (GetRootPath() + filename).c_str(), filename.c_str());
			}
		}
		catch (const std::exception& e)
		{
			LOG_W(TAG, "exception (%s) thrown by filesystem::remove(%s)", e.what(), (GetRootPath() + filename).c_str());
		}
	}

	// recursively call a functional *f* for each file that matches the expression
	int FileIO::ForEachFile(const std::string& search_path, const std::regex& regex, int depth, std::function<void(const std::string&)> folder_handler, std::function<void(const std::string&)> file_handler)
	{
		int n = 0;
		const std::filesystem::directory_iterator end;
		for (std::filesystem::directory_iterator iter{ search_path }; iter != end; iter++) {
			const std::string filename = iter->path().filename().string();
			if (std::filesystem::is_regular_file(*iter)) {
				if (std::regex_match(filename, regex)) {
					n++;
					file_handler(iter->path().string());
				}
			}
			else if (std::filesystem::is_directory(*iter) && depth > 0) {
				folder_handler(iter->path().string());
				n += ForEachFile(iter->path().string(), regex, depth - 1, folder_handler, file_handler);
			}
		}
		return n;
	}

	void FileIO::ReplaceStringInFolder(const String& source, const String& target, const String& file_name, const Array<String>& to_be_replaced, const Array<String>& replace_with, bool deep_replacing)
	{
		namespace fs = std::filesystem;
		const int MAX_DEPTH = 99;
		const std::regex star_replace("\\*");
		const std::regex questionmark_replace("\\?");
		auto wildcard_pattern = std::regex_replace(
			std::regex_replace(file_name, star_replace, ".*"),
			questionmark_replace, ".");
		int n = 0;
		if (!fs::exists(target))
			fs::create_directories(target);
		ForEachFile(
			source,
			std::regex(wildcard_pattern),
			deep_replacing ? MAX_DEPTH : 1,
			[&](const std::string& folder_name)
			{
				++n;
				LOG_D(TAG, "folder(%d): %s\n", n, folder_name.c_str());
				//ReplaceStringInFolder(folder_name, file_name, to_be_replaced, replace_with, deep_replace);
				String _target = target + folder_name.substr(source.size());
				if (!fs::exists(_target))
					fs::create_directories(_target);
			},
			[&](const std::string& file_name) {
				++n;
				LOG_D(TAG, "file(%d): %s\n", n, file_name.c_str());
				ReplaceStringInFile(file_name, target + file_name.substr(source.size()), to_be_replaced, replace_with);
			}
		);
	}

	void FileIO::ReplaceStringInFile(const String& source, const String& target, const Array<String>& to_be_replaced, const Array<String>& replace_with)
	{
		std::ifstream _ifs(source, std::ios::in);
		std::ofstream _ofs(target, std::ios::out);

		String _buf;
		while (std::getline(_ifs, _buf))
		{
			for (int i = 0; i < to_be_replaced.size(); i++)
			{
				_buf = std::regex_replace(_buf, std::regex(to_be_replaced[i]), replace_with[i]);
			}
			_ofs << _buf << "\n";
		}
	}
}