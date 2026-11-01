// Copyright (c) The Bitcoin Core developers
// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <bitcoin-build-config.h> // IWYU pragma: keep

#include <common/license_info.h>

#include <tinyformat.h>
#include <util/translation.h>

#include <string>

std::string CopyrightHolders(const std::string& strPrefix)
{
    const std::string copyright_devs{_(COPYRIGHT_HOLDERS)};
    std::string strCopyrightHolders = strprintf("%s 2026 %s", strPrefix, copyright_devs);

    // Make sure Bitcoin Core copyright is not removed by accident
    if (copyright_devs.find("Bitcoin Core") == std::string::npos) {
        strCopyrightHolders += strprintf("\n%s 2009-%i The Bitcoin Core developers", strPrefix, COPYRIGHT_YEAR);
    }
    return strCopyrightHolders;
}

std::string LicenseInfo()
{
    const std::string URL_SOURCE_CODE = "<https://github.com/luganoplanb/bitcoin-roots>";

    return CopyrightHolders(_("Copyright (C)")) + "\n" +
           "\n" +
           strprintf(_("Please contribute if you find %s useful. "
                       "Visit %s for further information about the software."),
                     CLIENT_NAME, "<" CLIENT_URL ">")
               .translated +
           "\n" +
           strprintf(_("The source code is available from %s."), URL_SOURCE_CODE).translated +
           "\n" +
           "\n" +
           _("This is experimental software.") + "\n" +
           strprintf(_("Distributed under the MIT software license, see the accompanying file %s or %s"), "COPYING", "<https://opensource.org/license/MIT>").translated +
           "\n";
}
