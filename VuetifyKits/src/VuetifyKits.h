#pragma once
#include "RNA.h"
#include "nlohmann/json.hpp"

namespace VuetifyKits
{

	class VuetifyKits : public BioSys::RNA
	{
	public:
		PUBLIC_API VuetifyKits(BioSys::IBiomolecule* owner);
		PUBLIC_API virtual ~VuetifyKits();

	protected:
		virtual void OnEvent(const DynaArray& name);
		String GetEventSourcePath();

	private:
		String EscapeJSONString(const String& in, bool escape_quote);
		String UnescapeJSONString(const String& in);
		void EscapeJSON(const String& in, String& out);
		void ReplaceModelNameWithValue(String& target, const String& resolved_model_name = "", bool recursive = true);
		bool findAndReplaceAll(String& data, const String& toSearch, const String& replaceStr);
		size_t FindRightBracket(const String& target, size_t pos, const Pair<char, char>& bracket);
		void ReplacePathIndex(String& path_str);
		void CheckUnknownItems(const String& id);

	private:
		template<class UnaryFunction>
		void recursive_iterate(nlohmann::json& j, UnaryFunction f);
		void ReadScriptFile(const String& filename, String& output);
		static Map<String,Mutex> file_reader_mutex_;
		Map<String, nlohmann::json::json_pointer> tree_view_map_;
		Map<String, nlohmann::json> unknown_tree_item_map_;
		nlohmann::json tree_view_;
	};

}