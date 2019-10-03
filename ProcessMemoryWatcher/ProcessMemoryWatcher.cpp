#include <windows.h>
#include <tlhelp32.h>

#include <iomanip>
#include <iostream>
#include <sstream>

#include <codecvt>
#include <string>
#include <tchar.h>


#pragma region https://stackoverflow.com/a/55030118

DWORD FindProcessId(const std::wstring &processName)
{
	PROCESSENTRY32 processInfo;
	processInfo.dwSize = sizeof(processInfo);

	HANDLE processesSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);
	if (processesSnapshot == INVALID_HANDLE_VALUE) {
		return 0;
	}

	Process32First(processesSnapshot, &processInfo);
	if (!processName.compare(processInfo.szExeFile))
	{
		CloseHandle(processesSnapshot);
		return processInfo.th32ProcessID;
	}

	while (Process32Next(processesSnapshot, &processInfo))
	{
		if (!processName.compare(processInfo.szExeFile))
		{
			CloseHandle(processesSnapshot);
			return processInfo.th32ProcessID;
		}
	}

	CloseHandle(processesSnapshot);
	return 0;
}

#pragma endregion

enum MemoryType : int
{
	Bool,
	Int,
	Float,
	String
};
enum MatchType : int
{
	DoNothing,
	CloseApp
};

void HexStringToInt(std::wstring str, size_t &out)
{
	std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
	std::stringstream ss; ss << std::hex << std::hex << converter.to_bytes(str.c_str()).c_str(); ss >> out;
}

std::wstring ToWideString(char *str)
{
	std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
	return converter.from_bytes(str);
}

struct Process
{
	std::wstring m_process;
	HANDLE m_pHandle;

	Process(const std::wstring& process)
	{
		m_process = process;
	}

	Process *Open()
	{
		m_pHandle = OpenProcess(PROCESS_ALL_ACCESS, FALSE, FindProcessId(m_process));

		return this;
	}

	bool HandleValid()
	{
		return m_pHandle != nullptr;
	}

	void Close()
	{
		DWORD result = WAIT_OBJECT_0;
		while (result == WAIT_OBJECT_0)
		{
			// use WaitForSingleObject to make sure it's dead
			result = WaitForSingleObject(m_pHandle, 100);
			TerminateProcess(m_pHandle, 0);
		}

		if (HandleValid())
		{
			CloseHandle(m_pHandle);
			m_pHandle = nullptr;
		}
	}

	template<typename m_type>
	Process *Read(size_t address, size_t offset, m_type *value)
	{
		if (HandleValid())
		{
			ReadProcessMemory(m_pHandle, reinterpret_cast<LPVOID>(address + offset), value, sizeof(m_type), NULL);
		}

		return this;
	}

	template<typename m_type>
	Process *Write(size_t address, size_t offset, m_type value)
	{
		if (HandleValid())
		{
			WriteProcessMemory(m_pHandle, reinterpret_cast<LPVOID>(address + offset), &value, sizeof(m_type), NULL);
		}

		return this;
	}

	void Match(MemoryType memoryType, size_t address, size_t offset, std::wstring value, MatchType matchType)
	{
		if (!value[0])
			return;

		bool run = false;

		static bool s_bool;
		static int s_int;
		static float s_float;
		static char s_string[1024];

		switch (memoryType)
		{
		case MemoryType::Bool:
			Read(address, offset, &s_bool);
			run = s_bool == static_cast<bool>(std::stoul(value.c_str()));
		case MemoryType::Int:
			Read(address, offset, &s_int);
			run = s_int == std::stoul(value);
		case MemoryType::Float:
			Read(address, offset, &s_float);
			run = s_float == std::stof(value);
		case MemoryType::String:
			Read(address, offset, &s_string);
			run = wcsstr(ToWideString(s_string).c_str(), value.c_str()) != 0;
		}

		if (run)
		{
			switch (matchType)
			{
			case CloseApp:
				Close();
				break;
			case DoNothing:
				break;
			}
		}
	}

	Process *Print(MemoryType type, size_t address, size_t offset)
	{
		static bool s_bool;
		static int s_int;
		static float s_float;
		static char s_string[1024];

		switch (type)
		{
		case MemoryType::Bool:
			Read(address, offset, &s_bool);
			printf_s("%s\n", s_bool ? "true" : "false");
			break;
		case MemoryType::Int:
			Read(address, offset, &s_int);
			printf_s("%i\n", s_int);
			break;
		case MemoryType::Float:
			Read(address, offset, &s_float);
			printf_s("%f\n", s_float);
			break;
		case MemoryType::String:
			Read(address, offset, &s_string);
			printf_s("%s\n", s_string);
			break;
		}

		return this;
	}

	DWORD_PTR AddBase(size_t &offet, std::wstring module = L"")
	{
		if (!module[0])
		{
			module = m_process;
		}

		static DWORD_PTR dwModuleBaseAddress = 0;
		if (!dwModuleBaseAddress)
		{
			HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, FindProcessId(m_process));
			if (hSnapshot != INVALID_HANDLE_VALUE)
			{
				MODULEENTRY32 ModuleEntry32;
				ModuleEntry32.dwSize = sizeof(MODULEENTRY32);
				if (Module32First(hSnapshot, &ModuleEntry32))
				{
					do
					{
						if (_tcsicmp(ModuleEntry32.szModule, module.c_str()) == 0)
						{
							dwModuleBaseAddress = (DWORD_PTR)ModuleEntry32.modBaseAddr;
							break;
						}
					} while (Module32Next(hSnapshot, &ModuleEntry32));
				}
				CloseHandle(hSnapshot);
			}
		}

		offet += dwModuleBaseAddress;
		return dwModuleBaseAddress;
	}
};

struct CommandLine
{
	int numArgs = 0;
	LPWSTR *szArgList;

	CommandLine()
	{
		szArgList = CommandLineToArgvW(GetCommandLineW(), &numArgs);
	}

	bool GetString(std::wstring argName, std::wstring &value, std::wstring defaultValue = L"")
	{
		value = defaultValue;

		if (szArgList && numArgs > 1)
		{
			for (int i = 1; i < numArgs; i++)
			{
				std::wstring arg = std::wstring(szArgList[i]);
				if (arg.compare(0, 1, L"-") != 0)
					continue;

				if (arg.compare(argName) == 0 && i < numArgs - 1)
				{
					value = szArgList[i + 1];
				}
			}
		}

		return value != defaultValue;
	}

	bool GetInt(std::wstring argName, int &value, int defaultValue = -1)
	{
		std::wstring out; GetString(argName, out, std::to_wstring(defaultValue));
		value = std::stoul(out);

		return value != defaultValue;
	}

	bool GetBool(std::wstring argName, bool &value, bool defaultValue = false)
	{
		int out; GetInt(argName, out, defaultValue);
		value = out;

		return value != defaultValue;
	}

	bool GetHex(std::wstring argName, size_t &value, size_t defaultValue = 0)
	{
		std::wstring out; GetString(argName, out, std::to_wstring(defaultValue));
		HexStringToInt(out.c_str(), value);

		return value != defaultValue;
	}
};

int main()
{
	CommandLine cli = CommandLine();

	static int s_sleep;
	static std::wstring s_processName;
	static std::wstring s_moduleName;
	static int s_type;
	static size_t s_address;
	static size_t s_offset;
	static std::wstring s_matchDelim;
	static MatchType s_matchType = MatchType::DoNothing;

	cli.GetInt(L"-sleep", s_sleep, 1000);
	cli.GetString(L"-process", s_processName);
	cli.GetString(L"-module", s_moduleName);
	cli.GetInt(L"-type", s_type);
	cli.GetHex(L"-address", s_address);
	cli.GetHex(L"-offset", s_offset);

	if (cli.GetString(L"-exit-on-match", s_matchDelim))
		s_matchType = MatchType::CloseApp;

	Process p = Process(s_processName);
	p.Open()->AddBase(s_address, s_moduleName[0] ? s_moduleName : s_processName);

	while (p.HandleValid())
	{
		if (s_type == -1 && s_moduleName[0])
			printf_s("[0x%IX, %S]\n", s_address, s_moduleName.c_str());

		p.Print(MemoryType(s_type), s_address, s_offset)->Match(MemoryType(s_type), s_address, s_offset, s_matchDelim, s_matchType);

		Sleep(s_sleep);
	}
}
