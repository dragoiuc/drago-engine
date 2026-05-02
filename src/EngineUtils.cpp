#include "EngineUtils.h"
#include "rapidjson/filereadstream.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"
#include <filesystem>
#include <iostream>
#include <cstdio>

void EngineUtils::ReadJsonFile(const std::string& path, rapidjson::Document& out_document)
{
	FILE* file_pointer = nullptr;
#ifdef _WIN32
	fopen_s(&file_pointer, path.c_str(), "rb");
#else
	file_pointer = fopen(path.c_str(), "rb");
#endif
	if (!file_pointer) {
		std::cout << "failed to open json file at [" << path << "]\n";
		std::exit(1);
	}
	char buffer[65536];
	rapidjson::FileReadStream stream(file_pointer, buffer, sizeof(buffer));
	out_document.ParseStream(stream);
	std::fclose(file_pointer);

	if (out_document.HasParseError()) {
		rapidjson::ParseErrorCode errorCode = out_document.GetParseError();
		std::cout << "error parsing json at [" << path << "]" << std::endl;
		exit(0);
	}
}

void EngineUtils::WriteJsonFile(const std::string& path, const rapidjson::Document& document)
{
	std::filesystem::create_directories(std::filesystem::path(path).parent_path());

	rapidjson::StringBuffer buffer;
	rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
	document.Accept(writer);

	FILE* file_pointer = nullptr;
#ifdef _WIN32
	fopen_s(&file_pointer, path.c_str(), "wb");
#else
	file_pointer = fopen(path.c_str(), "wb");
#endif
	if (!file_pointer) {
		std::cout << "failed to open json file for writing at [" << path << "]\n";
		std::exit(1);
	}

	std::fwrite(buffer.GetString(), 1, buffer.GetSize(), file_pointer);
	std::fclose(file_pointer);
}
