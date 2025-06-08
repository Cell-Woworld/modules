#include "ECPay.h"
#include "../proto/Payment.pb.h"
#include "UrlEncoder/Encoder.hpp"
#include "PicoSHA2/picosha2.h"
#include "pugixml/src/pugixml.hpp"

#define TAG "ECPay"

namespace ECPay
{

#ifndef STATIC_API
	extern "C" PUBLIC_API BioSys::RNA* CreateInstance(BioSys::IBiomolecule* owner)
	{
		return new ECPay(owner);
	}
#endif

	ECPay::ECPay(BioSys::IBiomolecule* owner)
		:RNA(owner, "ECPay", this)
	{
		init();
	}

	ECPay::~ECPay()
	{
	}

	void ECPay::OnEvent(const DynaArray& name)
	{
		USING_BIO_NAMESPACE
		using namespace Payment;
		const String& _name = name.str();
		switch (BioSys::hash(_name))
		{
		case "Payment.Order"_hash:
		{
			Map<String, String> _request_map;
			_request_map["PaymentType"] = "aio";
			_request_map["EncryptType"] = "1";
			_request_map["MerchantID"] = ReadValue<String>(_name + ".storeId");
			_request_map["ChoosePayment"] = ReadValue<String>(_name + ".choosePayment");
			_request_map["MerchantTradeNo"] = ReadValue<String>(_name + ".orderId");
			_request_map["MerchantTradeDate"] = ReadValue<String>(_name + ".orderTime");
			_request_map["TradeDesc"] = ReadValue<String>(_name + ".desc");
			_request_map["ReturnURL"] = ReadValue<String>(_name + ".returnURL");
			_request_map["InvoiceMark"] = "N";
			_request_map["PlatformID"] = "";
			_request_map["DeviceSource"] = "";
			double _total_amount = 0.;
			String _item_list;
			Array<String> _order_list = ReadValue<Array<String>>(name.str() + ".orderList");
			bool _exceed_max = false;
			for (const auto& elem : _order_list)
			{
				Order::ProductInfo _product_info;
				if (elem != "" && _product_info.ParseFromString(elem) == true)
				{
					String _next_product = _product_info.productname() + "X" + std::to_string(_product_info.amount());
					if (_item_list.size() + _next_product.size() >= 400)
					{
						_exceed_max = true;
					}
					else
					{
						_item_list += _next_product + "#";
					}
					//_total_amount += _product_info.amount() * _product_info.price();
					_total_amount += _product_info.price();
				}
			}
			if (_exceed_max)
				_item_list += "etc.";
			else if (_item_list.size() > 0)
				_item_list.pop_back();
			_request_map["ItemName"] = _item_list;
			_request_map["TotalAmount"] = std::to_string((int)_total_amount);
			String _check_mac_value = GenerateCheckMacValue((String)"HashKey=" + ReadValue<String>(_name +".token1"), (String)"HashIV=" + ReadValue<String>(_name + ".token2"), _request_map);
			String _form, _action;
			Order::Result _order_result;
			GenerateForm(*_order_result.mutable_form(), *_order_result.mutable_action(), ReadValue<String>(_name + ".serviceHost"), _request_map, _check_mac_value);
			SendEvent(Order::Result::descriptor()->full_name(), _order_result.SerializeAsString());
			break;
		}
		default:
			break;
		}
	}

	String ECPay::GenerateCheckMacValue(const String& head, const String& tail, const Map<String, String>& request_map)
	{
		String _link = "";
		_link = head + "&";
		for (const auto& elem : request_map)
		{
			_link += elem.first + "=" + elem.second + "&";
		}
		_link += tail;
		Encoder _encoder;
		_link = _encoder.dotNetUrlEncode(_link);
		std::transform(_link.begin(), _link.end(), _link.begin(),
			[](unsigned char c) { return std::tolower(c); });

		//LOG_I(TAG, "UrlEncode(Link): %s", _link.c_str());

		std::vector<unsigned char> _hash(picosha2::k_digest_size);
		picosha2::hash256(_link.begin(), _link.end(), _hash.begin(), _hash.end());
		String _hex_str = picosha2::bytes_to_hex_string(_hash.begin(), _hash.end());
		std::transform(_hex_str.begin(), _hex_str.end(), _hex_str.begin(),
			[](unsigned char c) { return std::toupper(c); });

		return _hex_str;
	}

	void ECPay::GenerateForm(String& form, String& action, const String& service_host, const Map<String, String>& request_map, const String& check_mac_value)
	{
		pugi::xml_document doc;
		pugi::xml_node _form = doc.append_child("form");
		_form.append_attribute("id") = "_form_aiochk";
		_form.append_attribute("method") = "post";
		_form.append_attribute("action") = ((String)"https://" + service_host + "/Cashier/AioCheckOut/V5").c_str();

		for (const auto& elem : request_map)
		{
			pugi::xml_node _content = _form.append_child("input");
			_content.append_attribute("type") = "hidden";
			_content.append_attribute("name") = elem.first.c_str();
			_content.append_attribute("id") = elem.first.c_str();
			_content.append_attribute("value") = elem.second.c_str();
		}

		pugi::xml_node _content = _form.append_child("input");
		_content.append_attribute("type") = "hidden";
		_content.append_attribute("name") = "CheckMacValue";
		_content.append_attribute("id") = "CheckMacValue";
		_content.append_attribute("value") = check_mac_value.c_str();

		//pugi::xml_node _script = _form.append_child("script");
		//_script.append_attribute("type") = "text/javascript";
		//_script.append_child(pugi::node_pcdata).set_value("document.getElementById('_form_aiochk').submit();");

		//doc.save(std::cout);
		//std::cout << std::endl;

		std::stringstream ss;
		doc.save(ss, "\t", pugi::format_raw);

		form = ss.str();
		std::replace(form.begin(), form.end(), '"', '\'');
		action = "document.getElementById('_form_aiochk').submit();";
	}
}