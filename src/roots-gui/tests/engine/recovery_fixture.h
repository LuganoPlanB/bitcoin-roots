// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once

#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <utility>

namespace roots::gui::test {
class RecoveryFixtureDirectory
{
public:
    RecoveryFixtureDirectory()
    {
        const auto base=std::filesystem::temp_directory_path();
        auto candidate=(base/"roots-engine-recovery-XXXXXX").string();
        if (!::mkdtemp(candidate.data())) throw std::runtime_error("create isolated recovery fixture");
        directory=std::move(candidate);
    }
    ~RecoveryFixtureDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(directory,error);
    }
    RecoveryFixtureDirectory(const RecoveryFixtureDirectory&)=delete;
    RecoveryFixtureDirectory& operator=(const RecoveryFixtureDirectory&)=delete;
    std::filesystem::path Metadata() const{return directory/"metadata.sqlite";}

private:
    std::filesystem::path directory;
};
}
