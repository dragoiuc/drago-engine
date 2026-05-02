#pragma once
#include <string>
#include "rapidjson/document.h"

/* Guarded Utility Class meant to handle utility logic for Engine */
namespace EngineUtils
{
	void ReadJsonFile(const std::string& path, rapidjson::Document& out_document);
	void WriteJsonFile(const std::string& path, const rapidjson::Document& document);
};
