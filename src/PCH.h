#pragma once
#define _AMD64_

#pragma warning(push)
#include "F4SE/F4SE.h"
#include "RE/Fallout.h"
#include <windows.h>

#ifdef NDEBUG
#	include <spdlog/sinks/basic_file_sink.h>
#else
#	include <spdlog/sinks/msvc_sink.h>
#endif
#pragma warning(pop)
#pragma warning(disable: 4100);
#pragma warning(disable: 4244);

#define DLLEXPORT __declspec(dllexport)
namespace logger = F4SE::log;

using namespace std::literals;

#include "Version.h"
