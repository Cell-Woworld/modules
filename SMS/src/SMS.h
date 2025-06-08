#pragma once
#include "RNA.h"

namespace SMS
{

class SMS : public BioSys::RNA
{
public:
	PUBLIC_API SMS(BioSys::IBiomolecule* owner);
	PUBLIC_API virtual ~SMS();

protected:
	virtual void OnEvent(const DynaArray& name);

private:
	template<typename ... Args>
	std::string string_format(const std::string& format, Args ... args) {
		size_t size = snprintf(nullptr, 0, format.c_str(), args ...) + 1; // Extra space for '\0'
		std::unique_ptr<char[]> buf(new char[size]);
		snprintf(buf.get(), size, format.c_str(), args ...);
		return std::string(buf.get(), buf.get() + size - 1); // We don't want the '\0' inside
	};
	void closeSocket(int sockfd);
};

}