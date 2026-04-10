#include <cwctype>
#include <iostream>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace {

std::wstring trimCpackPrefix(const std::wstring& value) {
    if (value == L"/--powershell-script") {
        return L"--powershell-script";
    }

    if (value.size() >= 4 && value[0] == L'/' && std::iswalpha(value[1]) && value[2] == L':' &&
        (value[3] == L'/' || value[3] == L'\\')) {
        return value.substr(1);
    }

    return value;
}

std::wstring quoteArgument(const std::wstring& value) {
    if (value.empty()) {
        return L"\"\"";
    }

    bool needsQuotes = false;
    for (const wchar_t ch : value) {
        if (ch == L' ' || ch == L'\t' || ch == L'"') {
            needsQuotes = true;
            break;
        }
    }

    if (!needsQuotes) {
        return value;
    }

    std::wstring quoted;
    quoted.push_back(L'"');
    size_t backslashCount = 0;
    for (const wchar_t ch : value) {
        if (ch == L'\\') {
            ++backslashCount;
            continue;
        }

        if (ch == L'"') {
            quoted.append(backslashCount * 2 + 1, L'\\');
            quoted.push_back(L'"');
            backslashCount = 0;
            continue;
        }

        if (backslashCount > 0) {
            quoted.append(backslashCount, L'\\');
            backslashCount = 0;
        }
        quoted.push_back(ch);
    }

    if (backslashCount > 0) {
        quoted.append(backslashCount * 2, L'\\');
    }
    quoted.push_back(L'"');
    return quoted;
}

int runProcess(const std::wstring& executable, const std::vector<std::wstring>& args) {
    std::wstring commandLine = quoteArgument(executable);
    for (const auto& arg : args) {
        commandLine.push_back(L' ');
        commandLine.append(quoteArgument(arg));
    }

    std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
    mutableCommandLine.push_back(L'\0');

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo{};

    if (!CreateProcessW(
            nullptr,
            mutableCommandLine.data(),
            nullptr,
            nullptr,
            FALSE,
            0,
            nullptr,
            nullptr,
            &startupInfo,
            &processInfo)) {
        std::wcerr << L"Failed to launch: " << executable << std::endl;
        return 1;
    }

    WaitForSingleObject(processInfo.hProcess, INFINITE);

    DWORD exitCode = 1;
    if (!GetExitCodeProcess(processInfo.hProcess, &exitCode)) {
        std::wcerr << L"Failed to read exit code for: " << executable << std::endl;
    }

    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    return static_cast<int>(exitCode);
}

std::wstring resolveMakensisPath() {
    if (const auto* explicitPath = _wgetenv(L"ND2_VIEWER_MAKENSIS_EXE")) {
        if (*explicitPath != L'\0') {
            return explicitPath;
        }
    }

    return L"makensis.exe";
}

}  // namespace

int wmain(int argc, wchar_t* argv[]) {
    std::vector<std::wstring> incomingArgs;
    incomingArgs.reserve(argc > 1 ? argc - 1 : 0);
    for (int i = 1; i < argc; ++i) {
        incomingArgs.emplace_back(trimCpackPrefix(argv[i]));
    }

    const auto makensisPath = resolveMakensisPath();

    if (incomingArgs.size() == 1 && incomingArgs.front() == L"/VERSION") {
        return runProcess(makensisPath, incomingArgs);
    }

    std::wstring powershellScript;
    std::vector<std::wstring> forwardedArgs;
    forwardedArgs.reserve(incomingArgs.size());

    for (size_t i = 0; i < incomingArgs.size(); ++i) {
        if (incomingArgs[i] == L"--powershell-script") {
            if (i + 1 >= incomingArgs.size()) {
                std::wcerr << L"Missing path after --powershell-script" << std::endl;
                return 1;
            }

            powershellScript = incomingArgs[++i];
            continue;
        }

        forwardedArgs.push_back(incomingArgs[i]);
    }

    if (powershellScript.empty()) {
        std::wcerr << L"Expected --powershell-script for NSIS packaging." << std::endl;
        return 1;
    }

    std::vector<std::wstring> powershellArgs = {
        L"-NoLogo",
        L"-NoProfile",
        L"-NonInteractive",
        L"-ExecutionPolicy",
        L"Bypass",
        L"-File",
        powershellScript,
    };
    powershellArgs.insert(powershellArgs.end(), forwardedArgs.begin(), forwardedArgs.end());

    return runProcess(L"powershell.exe", powershellArgs);
}
