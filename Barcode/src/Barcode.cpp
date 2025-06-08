#include "Barcode.h"
#include "../proto/Barcode.pb.h"
#include <filesystem>
#include <fstream>
#include "BarcodeFormat.h"
#include "BitMatrix.h"
#include "BitMatrixIO.h"
#include "CharacterSet.h"
#include "MultiFormatWriter.h"
#include "../stb/stb_image_write.h"

#define TAG "Barcode"

namespace Barcode
{

#ifndef STATIC_API
	extern "C" PUBLIC_API BioSys::RNA* CreateInstance(BioSys::IBiomolecule* owner)
	{
		return new Barcode(owner);
	}
#endif

	Barcode::Barcode(BioSys::IBiomolecule* owner)
		:RNA(owner, "Barcode", this)
	{
		init();
	}

	Barcode::~Barcode()
	{
	}

	void Barcode::OnEvent(const DynaArray& name)
	{
		USING_BIO_NAMESPACE
		const String& _name = name.str();
		switch (hash(_name))
		{
		case "Barcode.Generate"_hash:
		{
			using namespace ZXing;
			String _format_string = ReadValue<String>(_name + ".format");
			String _encoding_string = ReadValue<String>(_name + ".encoding");
			int _eccLevel = ReadValue<int>(_name + ".eccLevel");
			int _width = ReadValue<int>(_name + ".width");
			int _height = ReadValue<int>(_name + ".height");
			int _margin = ReadValue<int>(_name + ".margin");
			String _content = ReadValue<String>(_name + ".content");
			String _output_format = ReadValue<String>(_name + ".output_format");
			String _target_model_name = ReadValue<String>(_name + ".target_model_name");

			CharacterSet _encoding = CharacterSetFromString(_encoding_string);
			BarcodeFormat _format = BarcodeFormatFromString(_format_string);
			try {
				auto writer = MultiFormatWriter(_format).setMargin(_margin).setEncoding(_encoding).setEccLevel(_eccLevel);

				BitMatrix matrix;
				matrix = writer.encode(_content, _width, _height);
				if (_output_format == "" || _output_format == "PNG")
				{
					auto bitmap = ToMatrix<uint8_t>(matrix);
					int len = 0;
					char* png = (char*)stbi_write_png_to_mem(bitmap.data(), 0, bitmap.width(), bitmap.height(), 1, &len);
					WriteValue<String>(_target_model_name, String(png, len));
					STBIW_FREE(png);
				}
				else if (_output_format == "JPG")
				{
					auto bitmap = ToMatrix<uint8_t>(matrix);
					ss_.clear();
					stbi_write_jpg_to_func(stbi_handler_func, nullptr, bitmap.width(), bitmap.height(), 1, bitmap.data(), 0);
					WriteValue<String>(_target_model_name, ss_.str());
				}
				else if (_output_format == "SVG")
				{
					WriteValue<String>(_target_model_name, ToSVG(matrix));
				}
			}
			catch (const std::exception& e) {
				LOG_E(TAG, "Exception from Barcode.Generate: %s", e.what());
				WriteValue<String>(_target_model_name, "");
			}
			break;
		}
		default:
			break;
		}
	}

	std::stringstream Barcode::ss_;
	void Barcode::stbi_handler_func(void* context, void* data, int size)
	{
		ss_ << std::string((char*)data, size);
	}
}