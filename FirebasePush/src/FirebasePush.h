#pragma once
#include "RNA.h"

namespace FirebasePush
{

class FirebasePush : public BioSys::RNA
{
public:
	PUBLIC_API FirebasePush(BioSys::IBiomolecule* owner);
	PUBLIC_API virtual ~FirebasePush();

protected:
	virtual void OnEvent(const DynaArray& name);
};

}