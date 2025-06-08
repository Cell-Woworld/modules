#include "Punch.h"
#include "../proto/Punch.pb.h"

#define TAG "Punch"

namespace Punch
{

#ifndef STATIC_API
	extern "C" PUBLIC_API BioSys::RNA* CreateInstance(BioSys::IBiomolecule* owner)
	{
		return new Punch(owner);
	}
#endif

	Punch::Punch(BioSys::IBiomolecule* owner)
		:RNA(owner, "Punch", this)
	{
		init();
	}

	Punch::~Punch()
	{
	}

	void Punch::OnEvent(const DynaArray& name)
	{
		USING_BIO_NAMESPACE
		const String& _name = name.str();
		switch (hash(_name))
		{
		case "Punch.Action1"_hash:
		{
			String _id = ReadValue<String>(_name + ".id");
			std::cout << "****** Action name: " << _name << ", Content: " << _id << " ******" << "\n";
			break;
		}
		case "Punch.Action2"_hash:
		{
			String _id = ReadValue<String>(_name + ".id");
			std::cout << "****** Action name: " << _name << ", Content: " << _id << " ******" << "\n";
			break;
		}
		default:
			break;
		}
	}

}