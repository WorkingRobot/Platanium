#include "pch.h"
#include "curlhooks.h"

#include <iostream>

#define USE_CONSOLE

void* FindPattern(const char* pattern, size_t patternSize)
{
	MODULEINFO modInfo;
	GetModuleInformation(GetCurrentProcess(), GetModuleHandle(NULL), &modInfo, sizeof(modInfo));

	auto base = (size_t)modInfo.lpBaseOfDll;

	for (size_t i = 0; i < modInfo.SizeOfImage - patternSize; i++)
	{
		bool found = true;
		for (size_t j = 0; j < patternSize && found; j++)
		{
			found = pattern[j] == *(char*)(base + i + j);
		}
		if (found)
		{
			return (void*)(base + i);
		}
	}
	return NULL;
}

LPCVOID redir_func_ptr;
LPCVOID orig_func_ptr;

LONG ExcHandler(EXCEPTION_POINTERS* ExceptionInfo) {
	if (ExceptionInfo->ExceptionRecord->ExceptionCode == EXCEPTION_GUARD_PAGE) {
		if (ExceptionInfo->ContextRecord->Rip == (DWORD64)orig_func_ptr) {
			ExceptionInfo->ContextRecord->Rip = (DWORD64)redir_func_ptr;
		}
		ExceptionInfo->ContextRecord->EFlags |= 0x100; // http://www.c-jump.com/CIS77/ASM/Instructions/I77_0070_eflags_bits.htm (bit 8)
		return EXCEPTION_CONTINUE_EXECUTION;
	}
	else if (ExceptionInfo->ExceptionRecord->ExceptionCode == EXCEPTION_SINGLE_STEP) {
		DWORD oldFlags;
		VirtualProtect((LPVOID)orig_func_ptr, 1, PAGE_GUARD | PAGE_EXECUTE_READ, &oldFlags);
		return EXCEPTION_CONTINUE_EXECUTION;
	}
	return EXCEPTION_CONTINUE_SEARCH;
}

void EnableHook(const void* origFuncPtr, const void* redirFuncPtr) {
	MEMORY_BASIC_INFORMATION redirMemInfo;
	MEMORY_BASIC_INFORMATION origMemInfo;

	redir_func_ptr = redirFuncPtr;
	orig_func_ptr = origFuncPtr;
	if (VirtualQuery(orig_func_ptr, &origMemInfo, sizeof(origMemInfo)) && VirtualQuery(redir_func_ptr, &redirMemInfo, sizeof(redirMemInfo))) {
		if (origMemInfo.BaseAddress != redirMemInfo.BaseAddress) {
			if (AddVectoredExceptionHandler(1, ExcHandler)) {
				DWORD oldFlags;
				VirtualProtect((LPVOID)orig_func_ptr, 1, PAGE_GUARD | PAGE_EXECUTE_READ, &oldFlags);
			}
		}
	}
}

constexpr const char* CurlEasySetOptPattern = "\x89\x54\x24\x10\x4C\x89\x44\x24\x18\x4C\x89\x4C\x24\x20\x48\x83\xEC\x28\x48\x85\xC9\x75\x08\x8D\x41\x2B\x48\x83\xC4\x28\xC3\x4C";
constexpr const char* CurlSetOptPattern = "\x48\x89\x5C\x24\x08\x48\x89\x6C\x24\x10\x48\x89\x74\x24\x18\x57\x48\x83\xEC\x30\x33\xED\x49\x8B\xF0\x48\x8B\xD9";

void Main() {
#ifdef USE_CONSOLE
	AllocConsole();
	FILE* pFile;
	freopen_s(&pFile, "CONOUT$", "w", stdout);
#endif

	auto easy_find = FindPattern(CurlEasySetOptPattern, 0x20);
	auto set_find = FindPattern(CurlSetOptPattern, 0x1c);

	curl_easy_setopt = (decltype(curl_easy_setopt))easy_find;
	curl_setopt = (decltype(curl_setopt))set_find;

	EnableHook(curl_easy_setopt, curl_easy_setopt_detour);
}

BOOL WINAPI DllMain(HMODULE hModule, DWORD dwReason, LPVOID lpReserved)
{
	switch (dwReason)
	{
	case DLL_PROCESS_ATTACH:
		Main();
		break;
	case DLL_PROCESS_DETACH:
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	default:
		break;
	}
	return TRUE;
}