/*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant 
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <boost/algorithm/hex.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>

#include <osquery/core.h>
#include <osquery/filesystem.h>
#include <osquery/logger.h>
#include <osquery/tables.h>

namespace pt = boost::property_tree;
namespace fs = boost::filesystem;

namespace osquery {
namespace tables {

/// XProtect MatchFile key to column maps.
const std::map<std::string, std::string> kXProtectFileKeys = {
    {"NSURLNameKey", "filename"},
    {"NSURLTypeIdentifierKey", "filetype"},
};

/// Path to XProtect.meta.plist and XProtect.plist
const std::string kXProtectPath = 
    "/System/Library/CoreServices/"
    "CoreTypes.bundle/Contents/Resources/";

void genMatches(const pt::ptree& entry, std::vector<Row>& results) {
  if (entry.count("Matches") == 0) {
    return;
  }

  bool optional = (entry.get("MatchType", "") == "MatchAny");
  for (const auto& match : entry.get_child("Matches")) {
    if (match.second.count("Matches") > 0) {
      genMatches(match.second, results);
      continue;
    }

    Row r;
    r["optional"] = (optional) ? "1" : "0";
    r["identity"] = match.second.get("Identity", "");
    if (match.second.count("MatchFile") == 0) {
      // There is no file in this match entry, odd.
      continue;
    }

    // This can contain any of Foundation/Classes/NSURL_Class keys.
    auto fileinfo = match.second.get_child("MatchFile");
    if (fileinfo.count("LSDownloadContentTypeKey") > 0) {
      r["filetype"] = fileinfo.get<std::string>("LSDownloadContentTypeKey");
    } else {
      r["filetype"] = fileinfo.get("NSURLTypeIdentifierKey", "");
    }

    r["uses_pattern"] = (match.second.count("Pattern") > 0) ? "1" : "0";
    r["filename"] = fileinfo.get("NSURLNameKey", "");
    results.push_back(r);
  }
}

void genXProtectEntry(const pt::ptree &entry, QueryData& results) {
  // Entry is an XProtect dictionary of meta data about the item.
  auto name = entry.get("Description", "");
  auto launch_type = entry.get("LaunchServices.LSItemContentType", "");

  // Get the list of matches
  std::vector<Row> file_matches;
  genMatches(entry, file_matches);

  for (auto& r : file_matches) {
    r["name"] = name;
    r["launch_type"] = launch_type;
    results.push_back(r);
  }
}

QueryData genXProtectEntries(QueryContext& context) {
  QueryData results;
  pt::ptree tree;

  auto xprotect_path = fs::path(kXProtectPath) / "XProtect.plist";
  if (!osquery::pathExists(xprotect_path).ok()) {
    VLOG(1) << "XProtect.plist is missing";
    return results;
  }

  if (!osquery::parsePlist(xprotect_path, tree).ok()) {
    VLOG(1) << "Could not parse the XProtect.plist";
    return results;
  }

  if (tree.count("root") == 0) {
    // Empty plist.
    return results;
  }

  for (const auto& it : tree.get_child("root")) {
    genXProtectEntry(it.second, results);
  }

  return results;
}
}
}