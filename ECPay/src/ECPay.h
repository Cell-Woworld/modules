#pragma once
#include "RNA.h"

namespace ECPay
{

class ECPay : public BioSys::RNA
{
public:
	PUBLIC_API ECPay(BioSys::IBiomolecule* owner);
	PUBLIC_API virtual ~ECPay();

protected:
	virtual void OnEvent(const DynaArray& name);

private:
	String GenerateCheckMacValue(const String& head, const String& tail, const Map<String, String>& request_map);
	void GenerateForm(String& form, String& action, const String& service_host, const Map<String, String>& request_map, const String& check_mac_value);
};

}