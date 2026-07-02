/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    std::string sourceRoot()
    {
        const std::string self(__FILE__);
        const std::string suffix = "/tests/rtlog_policy_test.cpp";
        std::string::size_type pos = self.rfind(suffix);
        if (pos == std::string::npos) {
            std::cerr << "Could not locate OCL source root from " << self << std::endl;
            return std::string();
        }
        return self.substr(0, pos);
    }

    std::string readFile(const std::string& path)
    {
        std::ifstream stream(path.c_str());
        if (!stream) {
            std::cerr << "Could not open " << path << std::endl;
            return std::string();
        }

        std::ostringstream buffer;
        buffer << stream.rdbuf();
        return buffer.str();
    }

    std::string stripLineComment(const std::string& line)
    {
        std::string::size_type comment = line.find("//");
        if (comment == std::string::npos)
            return line;
        return line.substr(0, comment);
    }

    bool containsAny(const std::string& text, const char* const* needles, std::size_t needle_count)
    {
        for (std::size_t i = 0; i != needle_count; ++i) {
            if (text.find(needles[i]) != std::string::npos)
                return true;
        }
        return false;
    }

    bool hasLegacyStreamLog(const std::string& line)
    {
        const std::string code = stripLineComment(line);
        const char* stream_log_starts[] = {
            "log() <<",
            "log(Info) <<",
            "log(Debug) <<",
            "log(Warning) <<",
            "log(Error) <<",
            "log(Critical) <<",
            "log(Fatal) <<",
            "log(Logger::Info) <<",
            "log(Logger::Debug) <<",
            "log(Logger::Warning) <<",
            "log(Logger::Error) <<",
            "log(Logger::Critical) <<",
            "log(Logger::Fatal) <<",
            "Logger::log() <<",
            "Logger::log(Logger::Info) <<",
            "Logger::log(Logger::Debug) <<",
            "Logger::log(Logger::Warning) <<",
            "Logger::log(Logger::Error) <<",
            "Logger::log(Logger::Critical) <<",
            "Logger::log(Logger::Fatal) <<"
        };
        return containsAny(code, stream_log_starts, sizeof(stream_log_starts) / sizeof(stream_log_starts[0])) ||
               code.find("endlog") != std::string::npos ||
               code.find("Logger::In(") != std::string::npos ||
               code.find("Logger::endl") != std::string::npos ||
               code.find("Logger::nl") != std::string::npos;
    }
}

int main()
{
    const std::string root = sourceRoot();
    if (root.empty())
        return 1;

    const char* files[] = {
        "bin/deployer.cpp",
        "bin/cdeployer.cpp",
        "bin/deployer-corba.cpp",
        "bin/deployer-funcs.cpp",
        "lua/rttlua.cpp",
        "lua/LuaComponent.cpp",
        "lua/LuaService.cpp"
    };

    std::vector<std::string> violations;
    for (std::size_t file_index = 0; file_index != sizeof(files) / sizeof(files[0]); ++file_index) {
        const std::string relative = files[file_index];
        std::istringstream lines(readFile(root + "/" + relative));
        if (!lines)
            return 1;

        std::string line;
        int line_number = 0;
        while (std::getline(lines, line)) {
            ++line_number;
            if (hasLegacyStreamLog(line)) {
                std::ostringstream message;
                message << relative << ":" << line_number << ": " << line;
                violations.push_back(message.str());
            }
        }
    }

    if (!violations.empty()) {
        std::cerr << "OCL deployer logging must use Logger::log().logf(); first violation: "
                  << violations.front() << std::endl;
        return 1;
    }

    return 0;
}
